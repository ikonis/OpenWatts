#include "setup_wifi.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "board.h"
#include "settings_storage.h"
#include "web_ui.h"

namespace openwatts {
namespace {
constexpr char kTag[] = "setup_wifi";
constexpr char kSetupSsid[] = "OpenWatts-Setup";
constexpr uint8_t kEspImageMagic = 0xE9;

bool g_wifi_initialized = false;
httpd_handle_t g_httpd = nullptr;
TaskHandle_t g_dns_task = nullptr;
SetupWifi *g_portal = nullptr;
esp_timer_handle_t g_station_retry_timer = nullptr;
// HTTPD_DEFAULT_CONFIG gives handlers a small task stack.  Keep the rendered
// settings page in static storage: phones request it as part of captive-portal
// probing, and a 2.6 KiB local page buffer overflowed that task's stack.
char g_portal_page[9000]{};
char g_status_json[4096]{};

esp_err_t sendPage(httpd_req_t *req, const char *page) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

void rebootAfterOta(void *) {
    ESP_LOGI(kTag, "OTA complete; restarting into updated firmware");
    esp_restart();
}

void scheduleOtaReboot() {
    esp_timer_create_args_t args{.callback = &rebootAfterOta, .arg = nullptr,
                                 .dispatch_method = ESP_TIMER_TASK, .name = "ota_reboot",
                                 .skip_unhandled_events = false};
    esp_timer_handle_t timer = nullptr;
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        esp_timer_start_once(timer, 750000);
    }
}

esp_err_t otaUploadHandler(httpd_req_t *req) {
    if (!board::usbPresent()) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "OTA is available only while USB is connected");
    }
    if (req->content_len < 32) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware image is empty or invalid");
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (partition == nullptr || req->content_len > static_cast<int>(partition->size)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware image exceeds the update partition");
    }

    esp_ota_handle_t update{};
    esp_err_t err = esp_ota_begin(partition, req->content_len, &update);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "OTA begin failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not start OTA update");
    }

    uint8_t buffer[1024];
    int received = 0;
    bool image_header_checked = false;
    while (received < req->content_len) {
        const int wanted = std::min<int>(sizeof(buffer), req->content_len - received);
        const int got = httpd_req_recv(req, reinterpret_cast<char *>(buffer), wanted);
        if (got <= 0) {
            esp_ota_abort(update);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware upload interrupted");
        }
        if (!image_header_checked) {
            image_header_checked = true;
            if (buffer[0] != kEspImageMagic) {
                esp_ota_abort(update);
                return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not an ESP firmware binary");
            }
        }
        err = esp_ota_write(update, buffer, static_cast<size_t>(got));
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "OTA write failed: %s", esp_err_to_name(err));
            esp_ota_abort(update);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware write failed");
        }
        received += got;
    }

    err = esp_ota_end(update);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "OTA image validation failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware image validation failed");
    }
    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "OTA boot-partition selection failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not activate firmware image");
    }

    ESP_LOGI(kTag, "OTA wrote %d bytes to %s", received, partition->label);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"Update installed. Restarting now.\"}");
    scheduleOtaReboot();
    return ESP_OK;
}

esp_err_t otaPageHandler(httpd_req_t *req) {
    return sendPage(req, webui::kOtaPage);
    static constexpr char kPage[] =
        "<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>OpenWatts OTA Update</title><style>:root{color-scheme:dark;--bg:#111;--card:#1b1b1b;--line:#3a3a3a;--text:#f1f1f1;--muted:#aaa;--good:#63d68b;--bad:#ef7676}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif}header{position:sticky;top:0;background:#151515f2;border-bottom:1px solid var(--line)}.bar,main{max-width:1100px;margin:auto;padding:13px 14px}.bar{display:flex;align-items:center;gap:10px}.brand{font-size:20px;font-weight:800;margin-right:6px}nav{display:flex;gap:4px;overflow-x:auto;flex:1}a{color:var(--muted);text-decoration:none;padding:9px 10px;border-radius:8px;white-space:nowrap}a:hover,a.active{background:#303030;color:var(--text)}main{padding-top:22px}.card{max-width:620px;background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px}.sub{color:var(--muted);line-height:1.45}input{width:100%%;margin:12px 0;padding:10px;border:1px solid var(--line);border-radius:8px;background:#121212;color:var(--text)}button{border:1px solid #555;background:#303030;color:var(--text);border-radius:9px;padding:12px 14px;font:inherit;font-weight:700;cursor:pointer}button:disabled{opacity:.55;cursor:wait}.result{margin-top:14px;padding:11px;border:1px solid var(--line);border-radius:9px;min-height:42px}.good{color:var(--good)}.bad{color:var(--bad)}progress{width:100%%;height:12px;margin-top:14px}</style></head><body><header><div class=bar><span class=brand>OpenWatts</span><nav><a href=/>Status</a><a href=/settings>Settings</a><a href=/diagnostics>Diagnostics</a><a class=active href=/ota>OTA Update</a></nav></div></header><main><h1>Firmware Update</h1><section class=card><p class=sub>Available only while USB is connected. Select the PlatformIO <code>firmware.bin</code> file. The device validates the image, installs it, and restarts automatically.</p><input id=file type=file accept=.bin,application/octet-stream><button id=upload>Install Update</button><progress id=progress value=0 max=100 hidden></progress><div id=result class=result>Choose a firmware binary.</div></section><script>const f=document.querySelector('#file'),b=document.querySelector('#upload'),p=document.querySelector('#progress'),r=document.querySelector('#result');b.onclick=()=>{let x=f.files[0];if(!x){r.textContent='Choose a .bin firmware file first.';r.className='result bad';return}if(!x.name.toLowerCase().endsWith('.bin')||x.size<32||x.size>2*1024*1024){r.textContent='Select a valid firmware.bin under 2 MB.';r.className='result bad';return}b.disabled=true;p.hidden=false;p.value=0;r.textContent='Uploading '+x.name+'…';r.className='result';let q=new XMLHttpRequest();q.open('POST','/ota/upload');q.setRequestHeader('Content-Type','application/octet-stream');q.upload.onprogress=e=>{if(e.lengthComputable)p.value=Math.round(e.loaded/e.total*100)};q.onload=()=>{let ok=q.status>=200&&q.status<300;r.textContent=ok?'Update installed. Restarting now.':(q.responseText||'Update failed.');r.className='result '+(ok?'good':'bad');if(!ok)b.disabled=false};q.onerror=()=>{r.textContent='Upload connection failed.';r.className='result bad';b.disabled=false};q.send(x)}</script></main></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kPage, HTTPD_RESP_USE_STRLEN);
}

void wifiEventLog(void *, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            const auto *event = static_cast<wifi_event_ap_staconnected_t *>(event_data);
            ESP_LOGI(kTag, "setup client joined %02x:%02x:%02x:%02x:%02x:%02x (aid=%d)",
                     event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid);
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            const auto *event = static_cast<wifi_event_ap_stadisconnected_t *>(event_data);
            ESP_LOGW(kTag, "setup client left %02x:%02x:%02x:%02x:%02x:%02x (aid=%d)",
                     event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid);
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            const auto *event = static_cast<wifi_event_sta_disconnected_t *>(event_data);
            ESP_LOGW(kTag, "saved Wi-Fi connection ended (reason=%u)", static_cast<unsigned>(event->reason));
            if (g_station_retry_timer != nullptr) {
                esp_timer_stop(g_station_retry_timer);
                esp_timer_start_once(g_station_retry_timer, 3LL * 1000LL * 1000LL);
            }
        } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI(kTag, "saved Wi-Fi associated; waiting for DHCP");
            if (g_station_retry_timer != nullptr) esp_timer_stop(g_station_retry_timer);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
        const auto *event = static_cast<ip_event_ap_staipassigned_t *>(event_data);
        ESP_LOGI(kTag, "setup client received DHCP address " IPSTR, IP2STR(&event->ip));
    }
}

void retrySavedStation(void *) {
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) ESP_LOGW(kTag, "saved Wi-Fi retry did not start: %s", esp_err_to_name(err));
}

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

bool parseUnsigned(const std::string &text, uint32_t minimum, uint32_t maximum, uint32_t &value) {
    if (text.empty()) return false;
    errno = 0;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' || parsed < minimum || parsed > maximum) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool parseFloat(const std::string &text, float minimum, float maximum, float &value) {
    if (text.empty()) return false;
    errno = 0;
    char *end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(parsed) ||
        parsed < minimum || parsed > maximum) return false;
    value = parsed;
    return true;
}

bool validBleName(const std::string &name) {
    if (name.empty() || name.size() >= sizeof(DeviceConfig{}.ble_device_name)) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == ' ' || ch == '-' || ch == '_';
    });
}

esp_err_t badSetting(httpd_req_t *req, const char *message) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
}

esp_err_t benchSleepHandler(httpd_req_t *req) {
    if (g_portal == nullptr) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "portal not ready");
    g_portal->requestBenchLightSleep();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"Entering IMU-armed light sleep\"}");
}

esp_err_t benchHandler(httpd_req_t *req) {
    static constexpr char kPage[] =
        "<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>OpenWatts Bench Test</title><style>:root{color-scheme:dark;--bg:#111;--card:#1b1b1b;--line:#3a3a3a;--text:#f1f1f1;--muted:#aaa;--amber:#e7c86c}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif}header{position:sticky;top:0;background:#151515f2;border-bottom:1px solid var(--line)}.bar,main{max-width:1100px;margin:auto;padding:13px 14px}.bar{display:flex;gap:4px;align-items:center}.brand{font-size:20px;font-weight:800;margin-right:6px}nav{display:flex;gap:4px;flex:1;overflow-x:auto}a{color:var(--muted);text-decoration:none;padding:9px 10px;border-radius:8px;white-space:nowrap}a:hover,a.active{background:#303030;color:var(--text)}.card{max-width:760px;background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-top:14px}.sub{color:var(--muted)}button{border:1px solid #7b6830;background:#3b331d;color:var(--text);border-radius:9px;padding:12px 14px;font:inherit;font-weight:700;cursor:pointer}.result{margin-top:12px;padding:11px;border:1px solid var(--line);border-radius:9px;background:#121212}</style></head><body><header><div class=bar><span class=brand>OpenWatts</span><nav><a href=/>Status</a><a href=/settings>Settings</a><a href=/diagnostics>Diagnostics</a><a class=active href=/bench>Bench Test</a></nav></div></header><main><h1>Bench Sleep Test</h1><section class=card><h2>IMU-armed light sleep</h2><p class=sub>This stops BLE, Wi-Fi, and HX711 sampling, then enters the same light sleep used for battery operation. Move the board to wake it. The dashboard should return after wake when USB or the Wi-Fi maintenance override is active.</p><button id=sleep>Enter Light Sleep</button><div class=result id=result>Ready.</div></section><script>sleep.onclick=async()=>{sleep.disabled=true;result.textContent='Requesting sleep...';try{let r=await fetch('/bench/sleep',{method:'POST'});let d=await r.json();result.textContent=d.message+' Move the board after the page disconnects.'}catch(e){result.textContent=e.message}setTimeout(()=>sleep.disabled=false,4000)}</script></main></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t diagnosticsHandler(httpd_req_t *req) {
    return sendPage(req, webui::kDiagnosticsPage);
    static constexpr char kPage[] =
        "<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>OpenWatts Diagnostics</title><style>:root{color-scheme:dark;--bg:#111;--card:#1b1b1b;--line:#3a3a3a;--text:#f1f1f1;--muted:#aaa}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif}header{position:sticky;top:0;background:#151515f2;border-bottom:1px solid var(--line)}.bar,main{max-width:1100px;margin:auto;padding:13px 14px}.bar{display:flex;gap:10px;align-items:center}.brand{font-size:20px;font-weight:800;margin-right:6px}nav{display:flex;gap:4px;flex:1;overflow-x:auto}a{color:var(--muted);text-decoration:none;padding:9px 10px;border-radius:8px;white-space:nowrap}a:hover,a.active{background:#303030;color:var(--text)}.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px}.grid{display:grid;grid-template-columns:1fr;gap:12px}.sub,dt{color:var(--muted)}dl{display:grid;grid-template-columns:minmax(150px,1fr) auto;gap:9px 14px;margin:0}dd{margin:0;text-align:right;font-weight:650}@media(min-width:700px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}}</style></head><body><header><div class=bar><span class=brand>OpenWatts</span><nav><a href=/>Status</a><a href=/settings>Settings</a><a class=active href=/diagnostics>Diagnostics</a><a href=/ota>OTA Update</a></nav></div></header><main><h1>Diagnostics</h1><p class=sub>Live hardware bring-up data. Values refresh once per second.</p><section class=grid><section class=card><h2>Battery & USB</h2><dl><dt>Battery voltage</dt><dd id=voltage>--</dd><dt>Battery estimate</dt><dd id=battery>--</dd><dt>USB present</dt><dd id=usb>--</dd><dt>Charge status</dt><dd id=charging>--</dd></dl></section><section class=card><h2>IMU</h2><dl><dt>WHO_AM_I</dt><dd id=who>--</dd><dt>Interrupt line</dt><dd id=interrupt>--</dd><dt>Accelerometer X / Y / Z</dt><dd id=accel>--</dd><dt>Gyro X / Y / Z</dt><dd id=gyro>--</dd><dt>Detected revolutions</dt><dd id=revs>--</dd></dl></section><section class=card><h2>HX711</h2><dl><dt>Connection</dt><dd id=hx>--</dd><dt>Raw counts</dt><dd id=raw>--</dd><dt>Filtered counts</dt><dd id=filtered>--</dd><dt>Read failures</dt><dd id=failures>--</dd></dl></section><section class=card><h2>Runtime</h2><dl><dt>Uptime</dt><dd id=uptime>--</dd><dt>BLE</dt><dd id=ble>--</dd><dt>Wi-Fi override</dt><dd id=wifi>--</dd><dt>Calibration</dt><dd id=cal>--</dd></dl></section></section><script>const p=id=>document.getElementById(id),f=(v,n=2)=>Number(v).toFixed(n);function refresh(){fetch('/status',{cache:'no-store'}).then(r=>r.json()).then(s=>{p('voltage').textContent=s.battery_valid?f(s.battery_voltage)+' V':'Unavailable';p('battery').textContent=s.battery_valid?s.battery_percent+'%':'--';p('usb').textContent=s.usb?'Connected':'Disconnected';p('charging').textContent=s.charging?'Active':'Inactive';p('who').textContent='0x'+s.imu_whoami.toString(16).padStart(2,'0');p('interrupt').textContent=s.imu_interrupt?'High':'Low';p('accel').textContent=s.accel.map(v=>f(v,3)+' g').join(' / ');p('gyro').textContent=s.gyro.map(v=>f(v,1)+' dps').join(' / ');p('revs').textContent=s.revolutions;p('hx').textContent=s.hx?'Detected':'Unavailable';p('raw').textContent=s.raw;p('filtered').textContent=f(s.filtered);p('failures').textContent=s.failures;p('uptime').textContent=Math.floor(s.uptime/3600)+'h '+Math.floor(s.uptime%3600/60)+'m';p('ble').textContent=s.ble?'Connected':'Advertising';p('wifi').textContent=s.wifi_battery?'Battery override on':'USB only';p('cal').textContent=s.calibrated?'Valid':'Not calibrated';}).catch(()=>{})}refresh();setInterval(refresh,1000)</script></main></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kPage, HTTPD_RESP_USE_STRLEN);
}

[[maybe_unused]] esp_err_t modernSettingsHandler(httpd_req_t *req);

esp_err_t settingsHandler(httpd_req_t *req) {
    return sendPage(req, webui::kSettingsPage);
    const DeviceConfig *config = g_portal != nullptr ? g_portal->mutableConfig() : nullptr;
    if (config == nullptr) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "settings unavailable");
    std::snprintf(g_portal_page, sizeof(g_portal_page),
        "<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>OpenWatts Settings</title><style>:root{color-scheme:dark;--bg:#111;--card:#1b1b1b;--line:#3a3a3a;--text:#f1f1f1;--muted:#aaa}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif}header{position:sticky;top:0;background:#151515f2;border-bottom:1px solid var(--line)}.bar,main{max-width:760px;margin:auto;padding:13px 14px}.bar{display:flex;gap:14px;align-items:center}.brand{font-size:20px;font-weight:800;margin-right:auto}a{color:var(--muted);text-decoration:none;padding:9px;border-radius:8px}a:hover,a.active{background:#303030;color:var(--text)}.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-top:14px}h1{font-size:25px}h2{font-size:17px;margin:0 0 8px}.sub{color:var(--muted)}label{display:block;color:var(--muted);margin:14px 0 5px}input{width:100%%;border:1px solid #505050;background:#121212;color:var(--text);border-radius:8px;padding:11px;font:inherit}.check{display:flex;align-items:center;gap:9px;color:var(--text)}.check input{width:auto}button{border:1px solid #555;background:#303030;color:var(--text);border-radius:9px;padding:12px 14px;font:inherit;font-weight:700;cursor:pointer;margin-top:14px}.result{margin-top:12px;padding:11px;border:1px solid var(--line);border-radius:9px;background:#121212;white-space:pre-wrap}</style></head><body><header><div class=bar><span class=brand>OpenWatts</span><a href=/>Status</a><a class=active href=/settings>Settings</a><a href=/diagnostics>Diagnostics</a></div></header><main><h1>Settings</h1><p class=sub>Changes apply immediately and are retained on the device.</p><form class=card method=post action=/save><h2>Network</h2><label>Wi-Fi network</label><input name=ssid value='%s' maxlength=32><label>Password</label><input name=password type=password maxlength=64><label class=check><input type=checkbox name=wifi_battery%s> Keep Wi-Fi on without USB</label><p class=sub>Maintenance override; keeps the dashboard available on battery.</p><h2>MQTT</h2><label class=check><input type=checkbox name=mqtt_enabled%s> Enable MQTT battery reporting</label><label>Broker</label><input name=mqtt_host value='%s' maxlength=63><label>Port</label><input name=mqtt_port value='%u' inputmode=numeric><label>Topic</label><input name=mqtt_topic value='%s' maxlength=95><h2>Sleep</h2><label class=check><input type=checkbox name=sleep%s> Motion-wake sleep enabled</label><label>Inactivity delay, ms</label><input name=timeout value='%u' inputmode=numeric><button type=submit>Save Settings</button></form></main></body></html>",
        config->wifi_ssid, config->wifi_keep_alive_without_usb ? " checked" : "",
        config->mqtt_battery_notifications_enabled ? " checked" : "", config->mqtt_host,
        static_cast<unsigned>(config->mqtt_port), config->mqtt_topic,
        config->light_sleep_enabled ? " checked" : "", static_cast<unsigned>(config->inactivity_timeout_ms));
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, g_portal_page, HTTPD_RESP_USE_STRLEN);
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
    esp_netif_create_default_wifi_sta();

    esp_timer_create_args_t retry_args{.callback = &retrySavedStation, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
                                       .name = "wifi_retry", .skip_unhandled_events = false};
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry_args, &g_station_retry_timer), kTag, "create station retry timer");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &wifiEventLog, nullptr), kTag,
                        "register AP join event");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &wifiEventLog, nullptr), kTag,
                        "register AP leave event");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &wifiEventLog, nullptr), kTag,
                        "register STA connected event");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifiEventLog, nullptr), kTag,
                        "register STA disconnected event");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifiEventLog, nullptr), kTag,
                        "register DHCP event");

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        return err;
    }
    g_wifi_initialized = true;
    return ESP_OK;
}

[[maybe_unused]] esp_err_t modernStatusHandler(httpd_req_t *req);

esp_err_t rootHandler(httpd_req_t *req) {
    return sendPage(req, webui::kStatusPage);
    const DeviceConfig *config = g_portal != nullptr ? g_portal->mutableConfig() : nullptr;
    if (config == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "portal not ready");
    }
    std::snprintf(g_portal_page, sizeof(g_portal_page),
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>OpenWatts</title><style>body{font:16px system-ui;margin:0;background:#111827;color:#e5e7eb}main{max-width:760px;margin:auto;padding:18px}h1{margin:.1em 0}.sub{color:#9ca3af}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}.card,form{background:#1f2937;border:1px solid #374151;border-radius:10px;padding:13px;margin:14px 0}.label{color:#9ca3af;font-size:.8em;text-transform:uppercase}.value{font-size:1.25em;font-weight:650;margin-top:4px}label{display:block;margin:10px 0}input{box-sizing:border-box;width:100%%;padding:8px;margin-top:4px;border-radius:6px;border:1px solid #4b5563;background:#111827;color:#fff}input[type=checkbox]{width:auto}button{padding:9px 14px;border:0;border-radius:7px;background:#d1d5db;color:#111827;font-weight:650}</style></head><body><main><h1>OpenWatts</h1><div class='sub'>USB bring-up dashboard / <span id='uptime'>--</span></div><section class='grid'>"
        "<div class='card'><div class='label'>Power</div><div class='value' id='power'>-- W</div></div><div class='card'><div class='label'>Cadence</div><div class='value' id='cadence'>-- RPM</div></div><div class='card'><div class='label'>Torque</div><div class='value' id='torque'>-- Nm</div></div><div class='card'><div class='label'>Battery</div><div class='value' id='battery'>--</div></div><div class='card'><div class='label'>USB / charge</div><div class='value' id='usb'>--</div></div><div class='card'><div class='label'>Connections</div><div class='value' id='links'>--</div></div><div class='card'><div class='label'>HX711</div><div class='value' id='hx'>--</div></div><div class='card'><div class='label'>IMU</div><div class='value' id='imu'>--</div></div></section><div class='card'><div class='label'>Raw sensor data</div><div id='raw'>--</div></div>"
        "<script>function up(){fetch('/status').then(r=>r.json()).then(s=>{uptime.textContent='Uptime: '+Math.floor(s.uptime/3600)+'h '+Math.floor(s.uptime%%3600/60)+'m';power.textContent=s.calibrated?s.power+' W':'--';cadence.textContent=s.cadence.toFixed(1)+' RPM';torque.textContent=s.calibrated?s.torque.toFixed(2)+' Nm':'Not calibrated';battery.textContent=s.battery_valid?s.battery_voltage.toFixed(2)+' V / '+s.battery_percent+'%%':'Unavailable';usb.textContent=s.usb?'USB connected'+(s.charging?' / charging':''):'Battery';links.textContent='Wi-Fi active'+(s.wifi_battery?' / battery override on':'')+' / BLE '+(s.ble?'connected':'advertising');hx.textContent=s.hx?'Detected':'Unavailable';imu.textContent=s.imu?'Ready':'Not ready';raw.textContent=s.hx?'HX711 raw '+s.raw+' / filtered '+s.filtered.toFixed(1)+' / failures '+s.failures+' / revolutions '+s.revolutions:'No stable HX711 signal / read failures '+s.failures;})}up();setInterval(up,1000)</script>"
        "<form method='post' action='/save'><h2>Network & settings</h2>"
        "<label>Wi-Fi SSID <input name='ssid' value='%s' maxlength='32'></label><br>"
        "<label>Password <input name='password' type='password' maxlength='64'></label><br>"
        "<label><input type='checkbox' name='wifi_battery'%s> Keep Wi-Fi on without USB</label><br>"
        "<h2>Battery reporting</h2>"
        "<label><input type='checkbox' name='mqtt_enabled'%s> Enable MQTT battery reporting</label><br>"
        "<label>MQTT broker <input name='mqtt_host' value='%s' maxlength='63'></label><br>"
        "<label>MQTT port <input name='mqtt_port' value='%u' inputmode='numeric'></label><br>"
        "<label>MQTT topic <input name='mqtt_topic' value='%s' maxlength='95'></label><br>"
        "<h2>Power</h2><label><input type='checkbox' name='sleep'%s> Motion-wake sleep enabled</label><br>"
        "<label>Inactivity timeout ms (5000 minimum) <input name='timeout' value='%u'></label><br>"
        "<button type='submit'>Save</button></form>"
        "<p><a href='/selftest'>Self-test status</a></p></body></html>",
        config->wifi_ssid, config->wifi_keep_alive_without_usb ? " checked" : "",
        config->mqtt_battery_notifications_enabled ? " checked" : "", config->mqtt_host,
        static_cast<unsigned>(config->mqtt_port), config->mqtt_topic,
        config->light_sleep_enabled ? " checked" : "", static_cast<unsigned>(config->inactivity_timeout_ms));
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, g_portal_page, HTTPD_RESP_USE_STRLEN);
}

[[maybe_unused]] esp_err_t modernStatusHandler(httpd_req_t *req) {
    static constexpr char kPage[] =
        "<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>OpenWatts Status</title><style>:root{color-scheme:dark;--bg:#111;--card:#1b1b1b;--line:#3a3a3a;--text:#f1f1f1;--muted:#aaa;--green:#63d68b}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif}header{position:sticky;top:0;z-index:3;background:#151515f2;border-bottom:1px solid var(--line);backdrop-filter:blur(10px)}.bar{max-width:1100px;margin:auto;padding:13px 14px;display:flex;align-items:center;gap:10px}.brand{font-size:20px;font-weight:800;margin-right:6px}.uptime{color:var(--muted);font-size:12px;font-style:italic;text-align:right}nav{display:flex;gap:4px;overflow-x:auto;flex:1}nav a{color:var(--muted);text-decoration:none;padding:9px 10px;border-radius:8px;white-space:nowrap}nav a.active,nav a:hover{background:#303030;color:var(--text)}main{max-width:1100px;margin:auto;padding:18px 14px 48px}.title{display:flex;justify-content:space-between;align-items:end;gap:12px;margin-bottom:16px}.title h1{margin:0;font-size:25px}.title small,.sub{color:var(--muted)}.grid{display:grid;grid-template-columns:1fr;gap:12px}.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px}.card h2{font-size:17px;margin:0 0 14px}.hero{font-size:38px;font-weight:800}dl{display:grid;grid-template-columns:minmax(130px,1fr) auto;gap:9px 14px;margin:0}dt{color:var(--muted)}dd{margin:0;text-align:right;font-weight:650}.good{color:var(--green)}@media(min-width:700px){.bar,main{padding-left:18px;padding-right:18px}.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.title h1{font-size:28px}}</style></head><body><header><div class=bar><div class=brand>OpenWatts</div><nav><a class=active href=/>Status</a><a href=/settings>Settings</a><a href=/diagnostics>Diagnostics</a></nav><span class=uptime id=uptime>Uptime: --</span></div></header><main><div class=title><h1>Device Status</h1><small>USB bring-up firmware</small></div><div class=grid><section class=card><h2>Power</h2><div class=hero id=power>--</div><div class=sub><span id=cadence>--</span> RPM / <span id=torque>--</span> Nm</div></section><section class=card><h2>Battery</h2><div class=hero id=battery>--</div><div class=sub id=voltage>Waiting for measurement</div></section><section class=card><h2>Connections</h2><dl><dt>USB</dt><dd id=usb>--</dd><dt>Wi-Fi</dt><dd id=wifi>--</dd><dt>Bluetooth</dt><dd id=ble>--</dd></dl></section><section class=card><h2>Sensor Health</h2><dl><dt>IMU</dt><dd id=imu>--</dd><dt>HX711</dt><dd id=hx>--</dd><dt>Calibration</dt><dd id=cal>--</dd></dl></section></div><script>const p=id=>document.getElementById(id);function up(){fetch('/status',{cache:'no-store'}).then(r=>r.json()).then(s=>{let h=Math.floor(s.uptime/3600),m=Math.floor(s.uptime%3600/60),x=Math.floor(s.uptime%60);p('uptime').textContent='Uptime: '+h+'h '+m+'m '+x+'s';p('power').textContent=s.calibrated?s.power+' W':'--';p('cadence').textContent=s.cadence.toFixed(1);p('torque').textContent=s.calibrated?s.torque.toFixed(2):'Not calibrated';p('battery').textContent=s.battery_valid?s.battery_percent+'%':'Unavailable';p('voltage').textContent=s.battery_valid?s.battery_voltage.toFixed(2)+' V':'Battery measurement unavailable';p('usb').textContent=s.usb?'Connected':'Disconnected';p('wifi').textContent='Active'+(s.wifi_battery?' / battery override':'');p('ble').textContent=s.ble?'Connected':'Advertising';p('imu').textContent=s.imu?'Ready':'Not ready';p('hx').textContent=s.hx?'Detected':'Unavailable';p('cal').textContent=s.calibrated?'Valid':'Not calibrated';})}up();setInterval(up,1000)</script></main></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kPage, HTTPD_RESP_USE_STRLEN);
}

[[maybe_unused]] esp_err_t modernSettingsHandler(httpd_req_t *req) {
    const DeviceConfig *config = g_portal != nullptr ? g_portal->mutableConfig() : nullptr;
    if (config == nullptr) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "settings unavailable");
    std::snprintf(g_portal_page, sizeof(g_portal_page),
        "<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>OpenWatts Settings</title>"
        "<style>:root{color-scheme:dark;--bg:#111;--card:#1b1b1b;--line:#3a3a3a;--text:#f1f1f1;--muted:#aaa;--good:#63d68b}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif}header{position:sticky;top:0;background:#151515f2;border-bottom:1px solid var(--line)}.bar,main{max-width:1100px;margin:auto;padding:13px 14px}.bar{display:flex;align-items:center;gap:10px}.brand{font-size:20px;font-weight:800;margin-right:6px}nav{display:flex;gap:4px;flex:1;overflow-x:auto}nav a{color:var(--muted);text-decoration:none;padding:9px 10px;border-radius:8px;white-space:nowrap}nav a:hover,nav a.active{background:#303030;color:var(--text)}.uptime{font-size:12px;font-style:italic;color:var(--muted)}.card{max-width:760px;background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-top:14px}h1{font-size:25px}h2{font-size:17px}.sub,label{color:var(--muted)}label{display:block;margin:14px 0 5px}input{width:100%%;padding:11px;border-radius:8px;border:1px solid #505050;background:#121212;color:var(--text);font:inherit}.check{display:flex;gap:9px;align-items:center;color:var(--text)}.check input{width:auto}button{margin-top:14px;padding:12px 14px;border:1px solid #555;border-radius:9px;background:#303030;color:var(--text);font:inherit;font-weight:700}.result{margin-top:12px;padding:11px;border:1px solid var(--line);border-radius:9px}.ok{color:var(--good)}</style></head><body><header><div class=bar><div class=brand>OpenWatts</div><nav><a href=/>Status</a><a class=active href=/settings>Settings</a><a href=/diagnostics>Raw Data</a><a href=/ota>OTA</a></nav><span class=uptime id=up>Uptime: --</span></div></header><main><h1>Settings</h1><p class=sub>Changes apply immediately and are retained on the device.</p><form id=form class=card><h2>Network</h2><label>Wi-Fi network</label><input name=ssid value='%s' maxlength=32><label>Password</label><input name=password type=password maxlength=64><label class=check><input type=checkbox name=wifi_battery%s> Keep Wi-Fi on without USB</label><h2>MQTT</h2><label class=check><input type=checkbox name=mqtt_enabled%s> Enable MQTT battery reporting</label><label>Broker</label><input name=mqtt_host value='%s'><label>Port</label><input name=mqtt_port value='%u'><label>Topic</label><input name=mqtt_topic value='%s'><h2>Sleep</h2><label class=check><input type=checkbox name=sleep%s> IMU motion-wake sleep enabled</label><label>Inactivity delay, ms</label><input name=timeout value='%u'><button>Save Settings</button><div class=result id=result>Ready.</div></form><script>const f=document.getElementById('form'),r=document.getElementById('result');f.onsubmit=async e=>{e.preventDefault();r.textContent='Saving…';let q=await fetch('/save',{method:'POST',body:new URLSearchParams(new FormData(f))});r.textContent=q.ok?'✓ Settings applied':await q.text();r.className='result '+(q.ok?'ok':'')};setInterval(()=>fetch('/status').then(x=>x.json()).then(s=>up.textContent='Uptime: '+Math.floor(s.uptime/3600)+'h '+Math.floor(s.uptime%%60)+'m'),1000)</script></main></body></html>",
        config->wifi_ssid, config->wifi_keep_alive_without_usb ? " checked" : "", config->mqtt_battery_notifications_enabled ? " checked" : "", config->mqtt_host, static_cast<unsigned>(config->mqtt_port), config->mqtt_topic, config->light_sleep_enabled ? " checked" : "", static_cast<unsigned>(config->inactivity_timeout_ms));
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, g_portal_page, HTTPD_RESP_USE_STRLEN);
}

esp_err_t statusHandler(httpd_req_t *req) {
    if (g_portal == nullptr) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "portal not ready");
    const LiveStatus s = g_portal->liveStatus();
    const DeviceConfig &c = *g_portal->mutableConfig();
    std::snprintf(g_status_json, sizeof(g_status_json),
                  "{\"uptime\":%u,\"battery_voltage\":%.3f,\"battery_percent\":%u,\"battery_valid\":%s,"
                  "\"usb\":%s,\"charging\":%s,\"wifi_battery\":%s,\"ble\":%s,\"imu\":%s,\"imu_interrupt\":%s,\"imu_whoami\":%u,"
                  "\"accel\":[%.3f,%.3f,%.3f],\"gyro\":[%.2f,%.2f,%.2f],\"hx\":%s,\"calibrated\":%s,\"raw\":%ld,"
                  "\"filtered\":%.2f,\"torque\":%.3f,\"cadence\":%.2f,\"power\":%d,\"revolutions\":%u,\"failures\":%u,"
                   "\"config\":{\"wifi_ssid\":\"%s\",\"wifi_keep_alive_without_usb\":%s,"
                   "\"mqtt_enabled\":%s,\"mqtt_host\":\"%s\",\"mqtt_port\":%u,\"mqtt_topic\":\"%s\","
                   "\"light_sleep_enabled\":%s,\"inactivity_timeout_ms\":%u,\"ble_device_name\":\"%s\","
                   "\"ride_diagnostics_enabled\":%s,\"debug_logging_enabled\":%s,"
                   "\"power_filter_alpha\":%.3f,\"maximum_valid_power_watts\":%u,"
                   "\"auto_ride_zero_enabled\":%s,\"ride_detection_enabled\":%s,"
                   "\"minimum_ride_duration_seconds\":%u,\"cadence_timeout_seconds\":%u,"
                   "\"imu_wake_threshold\":%u,\"imu_revolution_threshold_dps\":%.1f,"
                   "\"rotation_aware_power_enabled\":%s,\"ble_advertising_power_dbm\":%d,"
                   "\"ble_auto_advertise_enabled\":%s,\"zero_offset\":%ld,\"counts_per_nm\":%.3f}}",
                  static_cast<unsigned>(s.uptime_seconds), static_cast<double>(s.battery_voltage),
                  static_cast<unsigned>(s.battery_percent), s.battery_valid ? "true" : "false",
                  s.usb_present ? "true" : "false", s.charging ? "true" : "false",
                  g_portal->mutableConfig()->wifi_keep_alive_without_usb ? "true" : "false", s.ble_connected ? "true" : "false",
                  s.imu_ready ? "true" : "false", s.imu_interrupt_active ? "true" : "false",
                  static_cast<unsigned>(s.imu_who_am_i), static_cast<double>(s.imu_accel_g[0]),
                  static_cast<double>(s.imu_accel_g[1]), static_cast<double>(s.imu_accel_g[2]),
                  static_cast<double>(s.imu_gyro_dps[0]), static_cast<double>(s.imu_gyro_dps[1]),
                  static_cast<double>(s.imu_gyro_dps[2]), s.hx711_ready ? "true" : "false",
                  s.strain_calibration_valid ? "true" : "false", static_cast<long>(s.raw_counts),
                  static_cast<double>(s.filtered_counts), static_cast<double>(s.torque_nm), static_cast<double>(s.cadence_rpm),
                  static_cast<int>(s.power_watts), static_cast<unsigned>(s.revolutions), static_cast<unsigned>(s.hx711_failures),
                   c.wifi_ssid, c.wifi_keep_alive_without_usb ? "true" : "false",
                   c.mqtt_battery_notifications_enabled ? "true" : "false", c.mqtt_host,
                   static_cast<unsigned>(c.mqtt_port), c.mqtt_topic,
                   c.light_sleep_enabled ? "true" : "false", static_cast<unsigned>(c.inactivity_timeout_ms),
                   c.ble_device_name, c.ride_diagnostics_enabled ? "true" : "false",
                   c.debug_logging_enabled ? "true" : "false", static_cast<double>(c.power_filter_alpha),
                   static_cast<unsigned>(c.maximum_valid_power_watts),
                   c.auto_ride_zero_enabled ? "true" : "false", c.ride_detection_enabled ? "true" : "false",
                   static_cast<unsigned>(c.minimum_ride_duration_seconds),
                   static_cast<unsigned>(c.cadence_timeout_seconds), static_cast<unsigned>(c.imu_wake_threshold),
                   static_cast<double>(c.imu_revolution_threshold_dps),
                   c.rotation_aware_power_enabled ? "true" : "false",
                   static_cast<int>(c.ble_advertising_power_dbm),
                   c.ble_auto_advertise_enabled ? "true" : "false",
                   static_cast<long>(c.zero_offset_counts), static_cast<double>(c.counts_per_nm));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, g_status_json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t selfTestHandler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "Self-test runs at boot; read serial log or BLE diagnostics characteristic.", HTTPD_RESP_USE_STRLEN);
}

esp_err_t calibrationHandler(httpd_req_t *req) {
    return sendPage(req, webui::kCalibrationPage);
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

    const DeviceConfig &current = *g_portal->mutableConfig();
    DeviceConfig candidate = current;
    const std::string ssid = formValue(body, "ssid");
    const std::string password = formValue(body, "password");
    const std::string timeout = formValue(body, "timeout");
    const std::string mqtt_host = formValue(body, "mqtt_host");
    const std::string mqtt_port = formValue(body, "mqtt_port");
    const std::string mqtt_topic = formValue(body, "mqtt_topic");
    const std::string ble_name = formValue(body, "ble_name");
    const std::string power_alpha = formValue(body, "power_filter_alpha");
    const std::string maximum_power = formValue(body, "maximum_valid_power_watts");

    if (ssid.size() > 32) return badSetting(req, "Wi-Fi network name is too long");
    if (password.size() > 64) return badSetting(req, "Wi-Fi password is too long");
    if (mqtt_host.size() > 63) return badSetting(req, "MQTT broker address is too long");
    if (mqtt_topic.size() > 95) return badSetting(req, "MQTT topic is too long");

    uint32_t parsed_timeout = 0;
    uint32_t parsed_port = 0;
    if (!parseUnsigned(timeout, 5000, 3600000, parsed_timeout)) {
        return badSetting(req, "Sleep timeout must be between 5 seconds and 60 minutes");
    }
    if (!parseUnsigned(mqtt_port, 1, 65535, parsed_port)) {
        return badSetting(req, "MQTT port must be between 1 and 65535");
    }
    const bool mqtt_enabled = body.find("mqtt_enabled=on") != std::string::npos;
    if (mqtt_enabled && (mqtt_host.empty() || mqtt_topic.empty())) {
        return badSetting(req, "MQTT broker and topic are required when battery reporting is enabled");
    }
    if (!ble_name.empty() && !validBleName(ble_name)) {
        return badSetting(req, "Bluetooth name must use 1-18 letters, numbers, spaces, hyphens, or underscores");
    }
    float parsed_alpha = candidate.power_filter_alpha;
    if (!power_alpha.empty() && !parseFloat(power_alpha, 0.05F, 1.0F, parsed_alpha)) {
        return badSetting(req, "Power smoothing must be between 0.05 and 1.00");
    }
    uint32_t parsed_maximum = candidate.maximum_valid_power_watts;
    if (!maximum_power.empty() && !parseUnsigned(maximum_power, 500, 5000, parsed_maximum)) {
        return badSetting(req, "Maximum believable power must be between 500 and 5000 watts");
    }

    std::strncpy(candidate.wifi_ssid, ssid.c_str(), sizeof(candidate.wifi_ssid) - 1);
    // A blank password field means "leave the existing secret alone" so a
    // settings-only save cannot silently disconnect the device from its AP.
    if (!password.empty()) {
        std::strncpy(candidate.wifi_password, password.c_str(), sizeof(candidate.wifi_password) - 1);
    }
    candidate.wifi_ssid[sizeof(candidate.wifi_ssid) - 1] = '\0';
    candidate.wifi_password[sizeof(candidate.wifi_password) - 1] = '\0';
    candidate.light_sleep_enabled = body.find("sleep=on") != std::string::npos;
    candidate.wifi_keep_alive_without_usb = body.find("wifi_battery=on") != std::string::npos;
    candidate.mqtt_battery_notifications_enabled = mqtt_enabled;
    if (!mqtt_host.empty()) std::strncpy(candidate.mqtt_host, mqtt_host.c_str(), sizeof(candidate.mqtt_host) - 1);
    if (!mqtt_topic.empty()) std::strncpy(candidate.mqtt_topic, mqtt_topic.c_str(), sizeof(candidate.mqtt_topic) - 1);
    candidate.mqtt_port = static_cast<uint16_t>(parsed_port);
    candidate.mqtt_host[sizeof(candidate.mqtt_host) - 1] = '\0';
    candidate.mqtt_topic[sizeof(candidate.mqtt_topic) - 1] = '\0';
    candidate.inactivity_timeout_ms = parsed_timeout;
    if (!ble_name.empty()) {
        std::strncpy(candidate.ble_device_name, ble_name.c_str(), sizeof(candidate.ble_device_name) - 1);
        candidate.ble_device_name[sizeof(candidate.ble_device_name) - 1] = '\0';
    }
    candidate.ride_diagnostics_enabled = body.find("ride_diagnostics=on") != std::string::npos;
    candidate.debug_logging_enabled = body.find("debug_logging=on") != std::string::npos;
    candidate.power_filter_alpha = parsed_alpha;
    candidate.maximum_valid_power_watts = static_cast<uint16_t>(parsed_maximum);
    candidate.force_setup_portal = false;

    esp_err_t err = g_portal->storage()->save(candidate);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }
    *g_portal->mutableConfig() = candidate;
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"reboot_required\":true,\"message\":\"Settings saved\"}");
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

esp_err_t SetupWifi::begin(DeviceConfig &config, SettingsStorage &storage, bool usb_present, bool setup_requested,
                           bool allow_battery_reporting) {
    active_ = false;
    config_ = &config;
    storage_ = &storage;

    if (!usb_present && !config.wifi_keep_alive_without_usb && !allow_battery_reporting) {
        ESP_LOGI(kTag, "USB absent; Wi-Fi remains off");
        return ESP_OK;
    }

    if (!config.wifi_setup_on_usb && !setup_requested && !config.force_setup_portal) {
        ESP_LOGI(kTag, "USB present but setup Wi-Fi disabled by config");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ensureWifiInitialized(), kTag, "wifi init");

    // A saved network is the normal USB bring-up path.  Keep the radio in
    // plain station mode there: APSTA is only recovery/setup and adds needless
    // coexistence/channel changes while connecting to the router.
    const bool start_setup_ap = !config.hasWifiCredentials() || setup_requested || config.force_setup_portal;
    if (start_setup_ap) {
        wifi_config_t ap_cfg{};
        std::strncpy(reinterpret_cast<char *>(ap_cfg.ap.ssid), kSetupSsid, sizeof(ap_cfg.ap.ssid));
        ap_cfg.ap.ssid_len = std::strlen(kSetupSsid);
        ap_cfg.ap.channel = 6;
        ap_cfg.ap.max_connection = 2;
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(config.hasWifiCredentials() ? WIFI_MODE_APSTA : WIFI_MODE_AP),
                            kTag, "wifi mode");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg), kTag, "wifi ap config");
    } else {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), kTag, "wifi station mode");
    }
    if (config.hasWifiCredentials()) {
        wifi_config_t sta_cfg{};
        std::strncpy(reinterpret_cast<char *>(sta_cfg.sta.ssid), config.wifi_ssid, sizeof(sta_cfg.sta.ssid));
        std::strncpy(reinterpret_cast<char *>(sta_cfg.sta.password), config.wifi_password, sizeof(sta_cfg.sta.password));
        sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_cfg.sta.pmf_cfg.capable = true;
        sta_cfg.sta.pmf_cfg.required = false;
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg), kTag, "wifi sta config");
    }
    ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "wifi start");
    if (config.hasWifiCredentials()) {
        const esp_err_t connect_err = esp_wifi_connect();
        if (connect_err != ESP_OK) {
            ESP_LOGW(kTag, "station connect did not start: %s", esp_err_to_name(connect_err));
        }
    }
    ESP_RETURN_ON_ERROR(startHttpServer(), kTag, "http server");
    if (start_setup_ap) {
        ESP_RETURN_ON_ERROR(startDnsRedirect(), kTag, "dns redirect");
        ESP_LOGW(kTag, "setup portal active: SSID=%s, URL=http://192.168.4.1/", kSetupSsid);
    } else {
        ESP_LOGI(kTag, "USB maintenance: joining saved Wi-Fi as a station");
    }

    active_ = true;
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
        ESP_LOGI(kTag, "Wi-Fi stopped");
    }
    active_ = false;
}

DeviceConfig *SetupWifi::mutableConfig() {
    return config_;
}

SettingsStorage *SetupWifi::storage() {
    return storage_;
}

void SetupWifi::updateLiveStatus(const LiveStatus &status) {
    live_status_ = status;
}

LiveStatus SetupWifi::liveStatus() const {
    return live_status_;
}

void SetupWifi::requestBenchLightSleep() {
    bench_light_sleep_requested_.store(true);
}

bool SetupWifi::consumeBenchLightSleepRequest() {
    return bench_light_sleep_requested_.exchange(false);
}

esp_err_t SetupWifi::startHttpServer() {
    if (g_httpd != nullptr) {
        return ESP_OK;
    }
    g_portal = this;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.max_uri_handlers = 13;
    ESP_RETURN_ON_ERROR(httpd_start(&g_httpd, &cfg), kTag, "httpd_start");

    httpd_uri_t root{.uri = "/", .method = HTTP_GET, .handler = rootHandler, .user_ctx = nullptr};
    httpd_uri_t save{.uri = "/save", .method = HTTP_POST, .handler = saveHandler, .user_ctx = nullptr};
    httpd_uri_t selftest{.uri = "/selftest", .method = HTTP_GET, .handler = selfTestHandler, .user_ctx = nullptr};
    httpd_uri_t status{.uri = "/status", .method = HTTP_GET, .handler = statusHandler, .user_ctx = nullptr};
    httpd_uri_t settings{.uri = "/settings", .method = HTTP_GET, .handler = settingsHandler, .user_ctx = nullptr};
    httpd_uri_t diagnostics{.uri = "/diagnostics", .method = HTTP_GET, .handler = diagnosticsHandler, .user_ctx = nullptr};
    httpd_uri_t calibration{.uri = "/calibration", .method = HTTP_GET, .handler = calibrationHandler, .user_ctx = nullptr};
    httpd_uri_t bench{.uri = "/bench", .method = HTTP_GET, .handler = benchHandler, .user_ctx = nullptr};
    httpd_uri_t bench_sleep{.uri = "/bench/sleep", .method = HTTP_POST, .handler = benchSleepHandler, .user_ctx = nullptr};
    httpd_uri_t ota_page{.uri = "/ota", .method = HTTP_GET, .handler = otaPageHandler, .user_ctx = nullptr};
    httpd_uri_t ota_upload{.uri = "/ota/upload", .method = HTTP_POST, .handler = otaUploadHandler, .user_ctx = nullptr};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &root), kTag, "root handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &save), kTag, "save handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &selftest), kTag, "selftest handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &status), kTag, "status handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &settings), kTag, "settings handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &diagnostics), kTag, "diagnostics handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &calibration), kTag, "calibration handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &bench), kTag, "bench handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &bench_sleep), kTag, "bench sleep handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &ota_page), kTag, "ota page handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_httpd, &ota_upload), kTag, "ota upload handler");
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
