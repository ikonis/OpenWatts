#pragma once

#include "config.h"
#include "esp_err.h"

namespace openwatts {

class SettingsStorage;

class SetupWifi {
public:
    esp_err_t begin(DeviceConfig &config, SettingsStorage &storage, bool usb_present, bool setup_requested);
    bool active() const;
    void stop();
    DeviceConfig *mutableConfig();
    SettingsStorage *storage();

private:
    esp_err_t startHttpServer();
    esp_err_t startDnsRedirect();

    bool active_ = false;
    DeviceConfig *config_ = nullptr;
    SettingsStorage *storage_ = nullptr;
};

}  // namespace openwatts
