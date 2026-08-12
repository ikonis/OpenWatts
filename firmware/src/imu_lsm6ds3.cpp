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
constexpr uint8_t kRegCtrl6C = 0x15;
constexpr uint8_t kRegWakeUpSrc = 0x1B;
constexpr uint8_t kRegOutxLG = 0x22;
constexpr uint8_t kRegTapCfg = 0x58;
constexpr uint8_t kRegWakeUpThs = 0x5B;
constexpr uint8_t kRegWakeUpDur = 0x5C;
constexpr uint8_t kRegMd1Cfg = 0x5E;
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

    return configureActiveMode();
}

esp_err_t Lsm6ds3::configureActiveMode() {
    // BDU=1, auto-increment=1, INT active-high push-pull.
    ESP_RETURN_ON_ERROR(writeReg(kRegCtrl3C, 0x44), kTag, "CTRL3_C active");
    ESP_RETURN_ON_ERROR(writeReg(kRegTapCfg, 0x00), kTag, "TAP_CFG active");
    ESP_RETURN_ON_ERROR(writeReg(kRegMd1Cfg, 0x00), kTag, "MD1_CFG active");
    // 104 Hz accelerometer +/-4 g and gyro +/-2000 dps. A crank's
    // instantaneous angular velocity is strongly non-uniform through the
    // pedal stroke; +/-500 dps clips power-stroke peaks even around 65 RPM
    // average cadence and causes angle integration to miss revolutions.
    ESP_RETURN_ON_ERROR(writeReg(kRegCtrl1Xl, 0x48), kTag, "CTRL1_XL active");
    ESP_RETURN_ON_ERROR(writeReg(kRegCtrl2G, 0x4C), kTag, "CTRL2_G active");
    return clearWakeSource();
}

esp_err_t Lsm6ds3::configureWakeMode(uint8_t threshold, uint8_t duration) {
    // Accelerometer low-power mode, 12.5 Hz, +/-4 g; gyroscope powered down.
    ESP_RETURN_ON_ERROR(writeReg(kRegCtrl2G, 0x00), kTag, "CTRL2_G sleep");
    ESP_RETURN_ON_ERROR(writeReg(kRegCtrl6C, 0x10), kTag, "CTRL6_C XL low power");
    ESP_RETURN_ON_ERROR(writeReg(kRegCtrl1Xl, 0x18), kTag, "CTRL1_XL wake");
    ESP_RETURN_ON_ERROR(writeReg(kRegWakeUpThs, threshold & 0x3FU), kTag, "WAKE_UP_THS");
    ESP_RETURN_ON_ERROR(writeReg(kRegWakeUpDur, (duration & 0x03U) << 5U), kTag, "WAKE_UP_DUR");
    // Enable basic interrupts, slope filtering, non-latched output.
    ESP_RETURN_ON_ERROR(writeReg(kRegTapCfg, 0x80), kTag, "TAP_CFG wake");
    ESP_RETURN_ON_ERROR(writeReg(kRegMd1Cfg, 0x20), kTag, "MD1_CFG INT1_WU");
    return clearWakeSource();
}

esp_err_t Lsm6ds3::clearWakeSource(uint8_t *source) {
    uint8_t value = 0;
    const esp_err_t err = readReg(kRegWakeUpSrc, value);
    if (source != nullptr) {
        *source = value;
    }
    return err;
}

bool Lsm6ds3::read(ImuSample &sample) {
    uint8_t raw[12]{};
    if (readRegs(kRegOutxLG, raw, sizeof(raw)) != ESP_OK) {
        sample.valid = false;
        return false;
    }

    constexpr float gyro_lsb_per_dps = 70.0F / 1000.0F;
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
