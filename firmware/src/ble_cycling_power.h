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
    char device_name_[19] = "OpenWatts";
};

}  // namespace openwatts
