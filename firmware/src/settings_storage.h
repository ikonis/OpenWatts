#pragma once

#include "config.h"
#include "esp_err.h"

namespace openwatts {

class SettingsStorage {
public:
    esp_err_t begin();
    DeviceConfig load();
    esp_err_t save(const DeviceConfig &config);
    esp_err_t markSelfTestDone(DeviceConfig &config);

private:
    bool initialized_ = false;
};

}  // namespace openwatts
