#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace openwatts {

struct ImuSample {
    float accel_g[3]{};
    float gyro_dps[3]{};
    bool valid = false;
};

class Lsm6ds3 {
public:
    explicit Lsm6ds3(uint8_t address = 0x6A);
    esp_err_t begin();
    esp_err_t configureActiveMode();
    esp_err_t configureWakeMode(uint8_t threshold, uint8_t duration);
    esp_err_t clearWakeSource(uint8_t *source = nullptr);
    bool read(ImuSample &sample);
    uint8_t whoAmI() const;

private:
    esp_err_t writeReg(uint8_t reg, uint8_t value);
    esp_err_t readReg(uint8_t reg, uint8_t &value);
    esp_err_t readRegs(uint8_t start_reg, uint8_t *data, uint8_t len);

    uint8_t address_;
    uint8_t who_am_i_ = 0;
    i2c_master_dev_handle_t device_ = nullptr;
};

}  // namespace openwatts
