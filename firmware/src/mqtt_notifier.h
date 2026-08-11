#pragma once

#include <atomic>
#include <cstdint>

#include "esp_err.h"
#include "mqtt_client.h"

namespace openwatts {

// A deliberately short-lived client for retained battery telemetry.  It does
// not run during normal riding unless Wi-Fi has explicitly been enabled.
class MqttNotifier {
public:
    esp_err_t begin(const char *host, uint16_t port, const char *topic, const char *payload,
                    bool publish_discovery);
    void stop();
    bool running() const;
    bool complete() const;
    bool succeeded() const;

private:
    static void eventHandler(void *args, esp_event_base_t base, int32_t event_id, void *event_data);
    void onEvent(esp_mqtt_event_handle_t event);

    esp_mqtt_client_handle_t client_ = nullptr;
    char topic_[96]{};
    char payload_[896]{};
    bool publish_discovery_ = false;
    std::atomic<bool> running_{false};
    std::atomic<bool> complete_{false};
    std::atomic<bool> succeeded_{false};
    std::atomic<int> publish_message_id_{-1};
};

}  // namespace openwatts
