#pragma once

#include <atomic>

#include "esp_err.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "power_estimator.h"

namespace openwatts {

void encodeCyclingPowerMeasurement(const PowerSample &sample, uint8_t payload[8]);

class BleCyclingPowerService {
public:
    esp_err_t begin(const char *device_name, int8_t advertising_power_dbm = 0);
    bool connected() const;
    void notify(const PowerSample &sample);
    int lastNotifyResult() const;
    uint32_t notifySuccessCount() const;
    uint32_t notifyFailureCount() const;
    int16_t lastNotifiedPowerWatts() const;
    float lastNotifiedCadenceRpm() const;
    uint16_t lastNotifiedCrankRevolutions() const;
    uint16_t lastNotifiedCrankEventTime() const;
    int64_t lastNotifyUs() const;
    bool measurementSubscribed() const;
    uint32_t transmitEventCount() const;
    uint32_t transmitErrorCount() const;
    int lastTransmitStatus() const;
    uint32_t disconnectCount() const;
    int lastDisconnectReason() const;
    uint16_t connectionIntervalUnits() const;
    int8_t connectionRssiDbm();
    void setDiagnostics(const char *text);
    void stop();
    void resumeAdvertising();

    static int gapEvent(ble_gap_event *event, void *arg);
    static int gattAccess(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt *ctxt, void *arg);

private:
    static void onSync();
    static void hostTask(void *param);
    void advertise();
    int onGapEvent(ble_gap_event *event);
    int onGattAccess(uint16_t attr_handle, ble_gatt_access_ctxt *ctxt);

    std::atomic<bool> connected_{false};
    std::atomic<uint16_t> conn_handle_{0};
    std::atomic<int> last_notify_result_{0};
    std::atomic<uint32_t> notify_success_count_{0};
    std::atomic<uint32_t> notify_failure_count_{0};
    std::atomic<int16_t> last_notified_power_watts_{0};
    std::atomic<int32_t> last_notified_cadence_x100_{0};
    std::atomic<uint16_t> last_notified_crank_revolutions_{0};
    std::atomic<uint16_t> last_notified_crank_event_time_{0};
    std::atomic<int64_t> last_notify_us_{0};
    std::atomic<bool> measurement_subscribed_{false};
    std::atomic<uint32_t> transmit_event_count_{0};
    std::atomic<uint32_t> transmit_error_count_{0};
    std::atomic<int> last_transmit_status_{0};
    std::atomic<uint32_t> disconnect_count_{0};
    std::atomic<int> last_disconnect_reason_{0};
    std::atomic<uint16_t> connection_interval_units_{0};
    std::atomic<int8_t> connection_rssi_dbm_{-127};
    int8_t configured_power_dbm_ = 0;
    char device_name_[19] = "OpenWatts";
};

}  // namespace openwatts
