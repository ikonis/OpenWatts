#include "imu_lsm6ds3.h"

#include "board.h"
#include "esp_check.h"
#include "esp_log.h"

namespace openwatts {
namespace {
constexpr char kTag[] = "lsm6ds3";
constexpr uint8_t kRegWhoAmI = 0x0F;
constexpr uint8_t kRegCtrl1Xl = 0x10;
constexpr uint8_t kRegCtrl2G = 0x11;
constexpr uint8_t kRegCtrl3C = 0x12;
constexpr uint8_t kRegOutxLG = 0x22;
constexpr uint8_t kExpectedWhoAmI = 0x69;
constexpr int kI2cTimeoutMs = 100;

int16_t le16(const uint8_t *data) {
    return static_cast<int16_t>((static_cast<uint16_t>(data[1]) << 8U) | data[0]);
}
}  // namespace

Lsm6ds3::Lsm6ds3(uint8_t address) : address_(address) {}

esp_err_t Lsm6ds3::begin() {
    if (device_ == nullptr) {
        i2c_device_config_t dev_cfg{};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = address_;
        dev_cfg.scl_speed_hz = board::kI2cClockHz;
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(board::i2cBus(), &dev_cfg, &device_), kTag, "add device");
    }

    esp_err_t err = readReg(kRegWhoAmI, who_am_i_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "WHO_AM_I read failed: %s", esp_err_to_name(err));
        return err;
    }
    if (who_am_i_ != kExpectedWhoAmI) {
        ESP_LOGW(kTag, "unexpected WHO_AM_I=0x%02x, expected 0x%02x", who_am_i_, kExpectedWhoAmI);
    }

    // BDU=1, auto-increment=1.
    ESP_RETURN_ON_ERROR(writeReg(kRegCtrl3C, 0x44), kTag, "CTRL3_C");
    // 104 Hz accel, +/-4 g.
    ESP_RETURN_ON_ERROR(writeReg(kRegCtrl1Xl, 0x48), kTag, "CTRL1_XL");
    // 104 Hz gyro, 500 dps.
    ESP_RETURN_ON_ERROR(writeReg(kRegCtrl2G, 0x44), kTag, "CTRL2_G");
    return ESP_OK;
}

bool Lsm6ds3::read(ImuSample &sample) {
    uint8_t raw[12]{};
    if (readRegs(kRegOutxLG, raw, sizeof(raw)) != ESP_OK) {
        sample.valid = false;
        return false;
    }

    constexpr float gyro_lsb_per_dps = 17.50F / 1000.0F;
    constexpr float accel_lsb_per_g = 0.122F / 1000.0F;
    sample.gyro_dps[0] = static_cast<float>(le16(&raw[0])) * gyro_lsb_per_dps;
    sample.gyro_dps[1] = static_cast<float>(le16(&raw[2])) * gyro_lsb_per_dps;
    sample.gyro_dps[2] = static_cast<float>(le16(&raw[4])) * gyro_lsb_per_dps;
    sample.accel_g[0] = static_cast<float>(le16(&raw[6])) * accel_lsb_per_g;
    sample.accel_g[1] = static_cast<float>(le16(&raw[8])) * accel_lsb_per_g;
    sample.accel_g[2] = static_cast<float>(le16(&raw[10])) * accel_lsb_per_g;
    sample.valid = true;
    return true;
}

uint8_t Lsm6ds3::whoAmI() const {
    return who_am_i_;
}

esp_err_t Lsm6ds3::writeReg(uint8_t reg, uint8_t value) {
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(device_, data, sizeof(data), kI2cTimeoutMs);
}

esp_err_t Lsm6ds3::readReg(uint8_t reg, uint8_t &value) {
    return i2c_master_transmit_receive(device_, &reg, 1, &value, 1, kI2cTimeoutMs);
}

esp_err_t Lsm6ds3::readRegs(uint8_t start_reg, uint8_t *data, uint8_t len) {
    return i2c_master_transmit_receive(device_, &start_reg, 1, data, len, kI2cTimeoutMs);
}

}  // namespace openwatts
