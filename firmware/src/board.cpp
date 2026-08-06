#include "board.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"

#include <algorithm>

namespace openwatts::board {
namespace {
i2c_master_bus_handle_t g_i2c_bus = nullptr;
adc_oneshot_unit_handle_t g_adc = nullptr;
adc_cali_handle_t g_adc_calibration = nullptr;
}

esp_err_t initPins() {
    gpio_config_t output{};
    output.pin_bit_mask = 1ULL << kGreenLed;
    output.mode = GPIO_MODE_OUTPUT;
    output.pull_up_en = GPIO_PULLUP_DISABLE;
    output.pull_down_en = GPIO_PULLDOWN_DISABLE;
    output.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&output);
    if (err != ESP_OK) {
        return err;
    }
    setGreenLed(false);

    gpio_config_t input{};
    input.pin_bit_mask = (1ULL << kUsbPresent) | (1ULL << kChargeStat) | (1ULL << kBoot) | (1ULL << kImuInt);
    input.mode = GPIO_MODE_INPUT;
    input.pull_up_en = GPIO_PULLUP_DISABLE;
    input.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&input);
}

esp_err_t initI2c() {
    if (g_i2c_bus != nullptr) {
        return ESP_OK;
    }

    i2c_master_bus_config_t cfg{};
    cfg.i2c_port = kI2cPort;
    cfg.sda_io_num = kI2cSda;
    cfg.scl_io_num = kI2cScl;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = false;
    return i2c_new_master_bus(&cfg, &g_i2c_bus);
}

i2c_master_bus_handle_t i2cBus() {
    return g_i2c_bus;
}

esp_err_t initBatteryAdc() {
    if (g_adc != nullptr) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg{};
    unit_cfg.unit_id = ADC_UNIT_1;
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &g_adc);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg{};
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    chan_cfg.atten = ADC_ATTEN_DB_12;
    err = adc_oneshot_config_channel(g_adc, ADC_CHANNEL_4, &chan_cfg);
    if (err != ESP_OK) {
        return err;
    }

    adc_cali_curve_fitting_config_t calibration_cfg{};
    calibration_cfg.unit_id = ADC_UNIT_1;
    calibration_cfg.chan = ADC_CHANNEL_4;
    calibration_cfg.atten = ADC_ATTEN_DB_12;
    // Curve fitting uses the ESP32-C3 ADC calibration data when available.
    // A missing calibration scheme is handled as an invalid battery read.
    return adc_cali_create_scheme_curve_fitting(&calibration_cfg, &g_adc_calibration);
}

uint32_t readBatteryMillivolts() {
    if (g_adc == nullptr || g_adc_calibration == nullptr) {
        return 0;
    }

    // The 100 kOhm / 100 kOhm divider is deliberately high impedance.  Take
    // a compact burst and trim its extremes so dashboard and policy readings
    // do not follow individual ADC conversion noise.
    constexpr size_t kSamples = 15;
    constexpr size_t kTrim = 3;
    int millivolts[kSamples]{};
    size_t valid = 0;
    for (size_t i = 0; i < kSamples; ++i) {
        int raw = 0;
        int mv = 0;
        if (adc_oneshot_read(g_adc, ADC_CHANNEL_4, &raw) == ESP_OK &&
            adc_cali_raw_to_voltage(g_adc_calibration, raw, &mv) == ESP_OK) {
            millivolts[valid++] = mv;
        }
    }
    if (valid < (kTrim * 2U + 1U)) {
        return 0;
    }
    std::sort(millivolts, millivolts + valid);
    uint32_t total = 0;
    for (size_t i = kTrim; i < valid - kTrim; ++i) {
        total += static_cast<uint32_t>(millivolts[i]);
    }
    return total / static_cast<uint32_t>(valid - kTrim * 2U);
}

bool usbPresent() {
    return gpio_get_level(kUsbPresent) != 0;
}

bool chargeStatActive() {
    return gpio_get_level(kChargeStat) == 0;
}

bool bootButtonPressed() {
    return gpio_get_level(kBoot) == 0;
}

bool imuInterruptActive() {
    return gpio_get_level(kImuInt) != 0;
}

void setGreenLed(bool on) {
    gpio_set_level(kGreenLed, on ? 1 : 0);
}

}  // namespace openwatts::board
