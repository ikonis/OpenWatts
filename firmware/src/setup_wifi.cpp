#include "setup_wifi.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <string>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "settings_storage.h"

namespace openwatts {
namespace {
constexpr char kTag[] = "setup_wifi";
constexpr char kSetupSsid[] = "OpenWatts-Setup";

bool g_wifi_initialized = false;
httpd_handle_t g_httpd = nullptr;
TaskHandle_t g_dns_task = nullptr;
SetupWifi *g_portal = nullptr;

std::string urlDecode(const char *input) {
    std::string out;
    if (input == nullptr) {
        return out;
    }
    for (size_t i = 0; input[i] != '\0'; ++i) {
        if (input[i] == '+') {
            out.push_back(' ');
        } else if (input[i] == '%' && std::isxdigit(static_cast<unsigned char>(input[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(input[i + 2]))) {
            char hex[] = {input[i + 1], input[i + 2], '\0'};
            out.push_back(static_cast<char>(std::strtol(hex, nullptr, 16)));
            i += 2;
        } else {
            out.push_back(input[i]);
        }
    }
    return out;
}

std::string formValue(const std::string &body, const char *key) {
    const std::string prefix = std::string(key) + "=";
    size_t pos = body.find(prefix);
    if (pos == std::string::npos) {
        return {};
    }
    pos += prefix.size();
    const size_t end = body.find('&', pos);
    return urlDecode(body.substr(pos, end == std::string::npos ? std::string::npos : end - pos).c_str());
}

esp_err_t ensureWifiInitialized() {
    if (g_wifi_initialized) {
        return ESP_OK;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        return err;
    }
    g_wifi_initialized = true;
    return ESP_OK;
}

esp_err_t rootHandler(httpd_req_t *req) {
    const char page[] =
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>OpenWatts Setup</title></head><body><h1>OpenWatts Setup</h1>"
        "<form method='post' action='/save'>"
        "<label>Wi-Fi SSID <input name='ssid' maxlength='32'></label><br>"
        "<label>Password <input name='password' type='password' maxlength='64'></label><br>"
        "<label><input type='checkbox' name='sleep' checked> Deep sleep enabled</label><br>"
        "<label>Inactivity timeout ms <input name='timeout' value='60000'></label><br>"
        "<button type='submit'>Save</button></form>"
        "<p><a href='/selftest'>Self-test status</a></p></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

esp_err_t selfTestHandler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "Self-test runs at boot; read serial log or BLE diagnostics characteristic.", HTTPD_RESP_USE_STRLEN);
}

esp_err_t saveHandler(httpd_req_t *req) {
    if (g_portal == nullptr || g_portal->mutableConfig() == nullptr || g_portal->storage() == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "portal not ready");
    }

    std::string body;
    body.resize(static_cast<size_t>(req->content_len));
    int received = 0;
    while (received < req->content_len) {
        const int got = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (got <= 0) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        }
        received += got;
    }

    DeviceConfig &config = *g_portal->mutableConfig();
    const std::string ssid = formValue(body, "ssid");
    const std::string password = formValue(body, "password");
    const std::string timeout = formValue(body, "timeout");
    std::strncpy(config.wifi_ssid, ssid.c_str(), sizeof(config.wifi_ssid) - 1);
    std::strncpy(config.wifi_password, password.c_str(), sizeof(config.wifi_password) - 1);
    config.wifi_ssid[sizeof(config.wifi_ssid) - 1] = '\0';
    config.wifi_password[sizeof(config.wifi_password) - 1] = '\0';
    config.deep_sleep_enabled = body.find("sleep=on") != std::string::npos;
    if (!timeout.empty()) {
        config.inactivity_timeout_ms = std::max<uint32_t>(5000, static_cast<uint32_t>(std::strtoul(timeout.c_str(), nullptr, 10)));
    }
    config.force_setup_portal = false;

    esp_err_t err = g_portal->storage()->save(config);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "Saved. Power-cycle or reset OpenWatts.", HTTPD_RESP_USE_STRLEN);
}

void dnsTask(void *arg) {
    (void)arg;
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        vTaskDelete(nullptr);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(53);
    if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        closesocket(sock);
        vTaskDelete(nullptr);
        return;
    }

    uint8_t rx[256]{};
    while (true) {
        sockaddr_in source{};
        socklen_t source_len = sizeof(source);
        const int len = recvfrom(sock, rx, sizeof(rx), 0, reinterpret_cast<sockaddr *>(&source), &source_len);
        if (len < 12) {
            continue;
        }

        uint8_t tx[320]{};
        std::memcpy(tx, rx, len);
        tx[2] = 0x81;
        tx[3] = 0x80;
        tx[6] = 0x00;
        tx[7] = 0x01;
        int offset = len;
        tx[offset++] = 0xC0;
        tx[offset++] = 0x0C;
        tx[offset++] = 0x00;
        tx[offset++] = 0x01;
        tx[offset++] = 0x00;
        tx[offset++] = 0x01;
        tx[offset++] = 0x00;
        tx[offset++] = 0x00;
        tx[offset++] = 0x00;
        tx[offset++] = 0x3C;
        tx[offset++] = 0x00;
        tx[offset++] = 0x04;
        tx[offset++] = 192;
        tx[offset++] = 168;
        tx[offset++] = 4;
        tx[offset++] = 1;
        sendto(sock, tx, offset, 0, reinterpret_cast<sockaddr *>(&source), source_len);
    }
}
}

esp_err_t SetupWifi::begin(DeviceConfig &config, SettingsStorage &storage, bool usb_present, bool setup_requested) {
    active_ = false;
    config_ = &config;
    storage_ = &storage;

    const bool missing_credentials = !config.hasWifiCredentials();
    const bool allow_requested_usb_setup = usb_present && (setup_requested || config.force_setup_portal);
    if (!missing_credentials && !allow_requested_usb_setup) {
        ESP_LOGI(kTag, "USB not present; Wi-Fi stays off for battery runtime");
        return ESP_OK;
    }

    if (!missing_credentials && !config.wifi_setup_on_usb && !config.force_setup_portal) {
        ESP_LOGI(kTag, "USB present but setup Wi-Fi disabled by config");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ensureWifiInitialized(), kTag, "wifi init");

    wifi_config_t ap_cfg{};
    std::strncpy(reinterpret_cast<char *>(ap_cfg.ap.ssid), kSetupSsid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = std::strlen(kSetupSsid);
    ap_cfg.ap.channel = 6;
    ap_cfg.ap.max_connection = 2;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), kTag, "wifi mode ap");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg), kTag, "wifi ap config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "wifi start");
    ESP_RETURN_ON_ERROR(startHttpServer(), kTag, "http server");
    ESP_RETURN_ON_ERROR(startDnsRedirect(), kTag, "dns redirect");

    active_ = true;
    ESP_LOGW(kTag, "setup portal active: SSID=%s, URL=http://192.168.4.1/", kSetupSsid);
    return ESP_OK;
}

bool SetupWifi::active() const {
    return active_;
}

void SetupWifi::stop() {
    if (active_) {
        if (g_httpd != nullptr) {
            httpd_stop(g_httpd);
            g_httpd = nullptr;
        }
        if (g_dns_task != nullptr) {
            vTaskDelete(g_dns_task);
            g_dns_task = nullptr;
        }
        esp_wifi_stop();
    }
    active_ = false;
}

DeviceConfig *SetupWifi::mutableConfig() {
    return config_;
}

SettingsStorage *SetupWifi::storage() {
    return storage_;
}

esp_err_t SetupWifi::startHttpServer() {
    if (g_httpd != nullptr) {
        return ESP_OK;
    }
    g_portal = this;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 6;
    ESP_RETURN_ON_ERROR(httpd_start(&g_httpd, &cfg), kTag, "httpd_start");

    httpd_uri_t root{.uri = "/", .method = HTTP_GET, .handler = rootHandler, .user_ctx = nullptr};
    httpd_uri_t save{.uri = "/save", .method = HTTP_POST, .handler = saveHandler, .user_ctx = nullptr};
    httpd_uri_t selftest{.uri = "/selftest", .method = HTTP_GET, .handler = selfTestHandler, .user_ctx = nullptr};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &root), kTag, "root handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &save), kTag, "save handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &selftest), kTag, "selftest handler");
    return ESP_OK;
}

esp_err_t SetupWifi::startDnsRedirect() {
    if (g_dns_task != nullptr) {
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreate(dnsTask, "dns_redirect", 3072, nullptr, 4, &g_dns_task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

}  // namespace openwatts
