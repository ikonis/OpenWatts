#include "mqtt_notifier.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"

namespace openwatts {
namespace {
constexpr char kTag[] = "mqtt";

void publishDiscovery(esp_mqtt_client_handle_t client, const char *state_topic) {
    struct Entity { const char *id; const char *name; const char *key; const char *unit; const char *device_class; };
    static constexpr Entity entities[] = {
        {"battery_voltage", "Battery Voltage", "battery_voltage", "V", "voltage"},
        {"battery_estimated", "Estimated Battery", "estimated_percent", "%", "battery"},
        {"battery_status", "Battery State", "battery_status", "", ""},
        {"firmware_version", "Firmware Version", "firmware_version", "", ""},
        {"device_health", "Device Health", "device_health", "", ""},
    };
    char topic[128]{};
    char payload[640]{};
    for (const auto &entity : entities) {
        std::snprintf(topic, sizeof(topic), "homeassistant/sensor/openwatts/%s/config", entity.id);
        char optional[160]{};
        if (entity.unit[0] != '\0') {
            std::snprintf(optional, sizeof(optional), "\"unit_of_measurement\":\"%s\",\"state_class\":\"measurement\",\"device_class\":\"%s\",", entity.unit, entity.device_class);
        }
        std::snprintf(payload, sizeof(payload),
                      "{\"name\":\"%s\",\"unique_id\":\"openwatts_%s\",\"state_topic\":\"%s\","
                      "\"value_template\":\"{{ value_json.%s }}\",%s"
                      "\"device\":{\"identifiers\":[\"openwatts\"],\"name\":\"OpenWatts\","
                      "\"manufacturer\":\"ikonis\",\"model\":\"OpenWatts Cycling Power Meter\"}}",
                      entity.name, entity.id, state_topic, entity.key, optional);
        esp_mqtt_client_publish(client, topic, payload, 0, 1, true);
    }
}
}  // namespace

esp_err_t MqttNotifier::begin(const char *host, uint16_t port, const char *topic, const char *payload,
                              bool publish_discovery) {
    if (running() || host == nullptr || host[0] == '\0' || topic == nullptr || topic[0] == '\0' ||
        payload == nullptr || std::strlen(topic) >= sizeof(topic_) || std::strlen(payload) >= sizeof(payload_)) {
        return ESP_ERR_INVALID_ARG;
    }
    std::strncpy(topic_, topic, sizeof(topic_) - 1);
    std::strncpy(payload_, payload, sizeof(payload_) - 1);
    publish_discovery_ = publish_discovery;
    complete_.store(false);
    succeeded_.store(false);
    publish_message_id_.store(-1);

    esp_mqtt_client_config_t config{};
    config.broker.address.hostname = host;
    config.broker.address.port = port;
    config.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    config.credentials.client_id = "OpenWatts";
    config.network.timeout_ms = 5000;
    config.session.disable_clean_session = false;
    client_ = esp_mqtt_client_init(&config);
    if (client_ == nullptr) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &MqttNotifier::eventHandler, this);
    if (err == ESP_OK) err = esp_mqtt_client_start(client_);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
        return err;
    }
    running_.store(true);
    return ESP_OK;
}

void MqttNotifier::stop() {
    if (client_ != nullptr) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }
    running_.store(false);
}
bool MqttNotifier::running() const { return running_.load(); }
bool MqttNotifier::complete() const { return complete_.load(); }
bool MqttNotifier::succeeded() const { return succeeded_.load(); }
void MqttNotifier::eventHandler(void *args, esp_event_base_t, int32_t, void *event_data) {
    static_cast<MqttNotifier *>(args)->onEvent(static_cast<esp_mqtt_event_handle_t>(event_data));
}
void MqttNotifier::onEvent(esp_mqtt_event_handle_t event) {
    if (event == nullptr || client_ == nullptr) return;
    if (event->event_id == MQTT_EVENT_CONNECTED) {
        if (publish_discovery_) {
            publishDiscovery(client_, topic_);
            publish_discovery_ = false;
        }
        const int id = esp_mqtt_client_publish(client_, topic_, payload_, 0, 1, true);
        publish_message_id_.store(id);
        if (id < 0) complete_.store(true);
    } else if (event->event_id == MQTT_EVENT_PUBLISHED && event->msg_id == publish_message_id_.load()) {
        succeeded_.store(true);
        complete_.store(true);
        ESP_LOGI(kTag, "battery telemetry published");
    } else if (event->event_id == MQTT_EVENT_ERROR) {
        complete_.store(true);
        ESP_LOGW(kTag, "MQTT transport error");
    }
}

}  // namespace openwatts
