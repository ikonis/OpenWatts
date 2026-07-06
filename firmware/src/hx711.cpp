#include "hx711.h"

#include <algorithm>
#include <cmath>

#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace openwatts {
namespace {
portMUX_TYPE g_hx711_mux = portMUX_INITIALIZER_UNLOCKED;
}

Hx711::Hx711(gpio_num_t dout, gpio_num_t sck) : dout_(dout), sck_(sck) {}

esp_err_t Hx711::begin() {
    gpio_config_t input{};
    input.pin_bit_mask = 1ULL << dout_;
    input.mode = GPIO_MODE_INPUT;
    input.pull_up_en = GPIO_PULLUP_ENABLE;
    input.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&input);
    if (err != ESP_OK) {
        return err;
    }

    gpio_config_t output{};
    output.pin_bit_mask = 1ULL << sck_;
    output.mode = GPIO_MODE_OUTPUT;
    output.pull_up_en = GPIO_PULLUP_DISABLE;
    output.pull_down_en = GPIO_PULLDOWN_DISABLE;
    output.intr_type = GPIO_INTR_DISABLE;
    err = gpio_config(&output);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(sck_, 0);
    return ESP_OK;
}

bool Hx711::read(int32_t &value, uint32_t timeout_ms) {
    const TickType_t start = xTaskGetTickCount();
    while (gpio_get_level(dout_) != 0) {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(timeout_ms)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint32_t data = 0;
    taskENTER_CRITICAL(&g_hx711_mux);
    for (int i = 0; i < 24; ++i) {
        gpio_set_level(sck_, 1);
        esp_rom_delay_us(1);
        data = (data << 1U) | static_cast<uint32_t>(gpio_get_level(dout_));
        gpio_set_level(sck_, 0);
        esp_rom_delay_us(1);
    }
    gpio_set_level(sck_, 1);
    esp_rom_delay_us(1);
    gpio_set_level(sck_, 0);
    taskEXIT_CRITICAL(&g_hx711_mux);

    if (data & 0x800000U) {
        data |= 0xFF000000U;
    }
    value = static_cast<int32_t>(data);
    return true;
}

void Hx711::observe(bool success, int32_t raw_value, float smoothing) {
    smoothing = std::clamp(smoothing, 0.0F, 0.95F);
    if (!success) {
        ++read_failures_;
        ++consecutive_failures_;
        ready_ = consecutive_failures_ < 20 && has_filter_;
        return;
    }

    consecutive_failures_ = 0;
    ready_ = true;
    const float raw = static_cast<float>(raw_value);
    if (!has_filter_) {
        filtered_ = raw;
        noise_estimate_ = 0.0F;
        has_filter_ = true;
        return;
    }

    filtered_ = (filtered_ * smoothing) + (raw * (1.0F - smoothing));
    const float residual = std::fabs(raw - filtered_);
    noise_estimate_ = (noise_estimate_ * 0.95F) + (residual * 0.05F);
}

bool Hx711::ready() const {
    return ready_;
}

float Hx711::filtered() const {
    return ready_ ? filtered_ : 0.0F;
}

float Hx711::noiseEstimate() const {
    return ready_ ? noise_estimate_ : 0.0F;
}

uint32_t Hx711::readFailures() const {
    return read_failures_;
}

}  // namespace openwatts
