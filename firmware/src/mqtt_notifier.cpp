#include "mqtt_notifier.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"

namespace openwatts {
namespace {
constexpr char kTag[] = "mqtt";

void publishDiscovery(esp_mqtt_client_handle_t client, const char *state_topic) {
    struct Entity {
        const char *id;
        const char *name;
        const char *key;
        const char *unit;
        const char *device_class;
        bool measurement;
        bool ride_only;
    };
    static constexpr Entity entities[] = {
        {"battery_voltage", "Battery Voltage", "battery_voltage", "V", "voltage", true, false},
        {"battery_estimated", "Estimated Battery", "estimated_percent", "%", "battery", true, false},
        {"battery_status", "Battery State", "battery_status", "", "", false, false},
        {"firmware_version", "Firmware Version", "firmware_version", "", "", false, false},
        {"device_health", "Device Health", "device_health", "", "", false, false},
        {"last_ride_moving", "Last Ride Moving Time", "last_ride_moving_seconds", "s", "duration", true, true},
        {"last_ride_elapsed", "Last Ride Elapsed Time", "last_ride_elapsed_seconds", "s", "duration", true, true},
        {"last_ride_average_power", "Last Ride Average Power", "last_ride_average_power", "W", "power", true, true},
        {"last_ride_peak_power", "Last Ride Peak Power", "last_ride_peak_power", "W", "power", true, true},
        {"last_ride_average_cadence", "Last Ride Average Cadence", "last_ride_average_cadence", "rpm", "", true, true},
        {"last_ride_peak_cadence", "Last Ride Peak Cadence", "last_ride_peak_cadence", "rpm", "", true, true},
        {"last_ride_revolutions", "Last Ride Revolutions", "last_ride_revolutions", "rev", "", true, true},
        {"last_ride_work", "Last Ride Work", "last_ride_work_kj", "kJ", "energy", true, true},
        {"last_ride_end_reason", "Last Ride End Reason", "last_ride_end_reason", "", "", false, true},
    };
    char topic[128]{};
    char payload[640]{};
    for (const auto &entity : entities) {
        std::snprintf(topic, sizeof(topic), "homeassistant/sensor/openwatts/%s/config", entity.id);
        char optional[160]{};
        if (entity.unit[0] != '\0') {
            std::snprintf(optional, sizeof(optional),
                          "\"unit_of_measurement\":\"%s\",%s%s%s",
                          entity.unit,
                          entity.measurement ? "\"state_class\":\"measurement\"," : "",
                          entity.device_class[0] != '\0' ? "\"device_class\":\"" : "",
                          entity.device_class[0] != '\0' ? entity.device_class : "");
            if (entity.device_class[0] != '\0') std::strncat(optional, "\",", sizeof(optional) - std::strlen(optional) - 1);
        }
        char availability[160]{};
        if (entity.ride_only) {
            std::snprintf(availability, sizeof(availability),
                          "\"availability_topic\":\"%s\","
                          "\"availability_template\":\"{{ 'online' if value_json.last_ride_valid else 'offline' }}\",",
                          state_topic);
        }
        std::snprintf(payload, sizeof(payload),
                      "{\"name\":\"%s\",\"unique_id\":\"openwatts_%s\",\"state_topic\":\"%s\","
                      "\"value_template\":\"{{ value_json.%s }}\",%s%s"
                      "\"device\":{\"identifiers\":[\"openwatts\"],\"name\":\"OpenWatts\","
                      "\"manufacturer\":\"OpenWatts\",\"model\":\"OpenWatts Cycling Power Meter\"}}",
                      entity.name, entity.id, state_topic, entity.key, optional, availability);
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
