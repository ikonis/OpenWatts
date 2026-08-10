#include "hx711.h"

#include <algorithm>
#include <cmath>

#include "esp_rom_sys.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace openwatts {
namespace {
portMUX_TYPE g_hx711_mux = portMUX_INITIALIZER_UNLOCKED;
}

Hx711::Hx711(gpio_num_t dout, gpio_num_t sck) : dout_(dout), sck_(sck) {}

esp_err_t Hx711::begin() {
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(sck_);
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
    monitoring_started_us_ = esp_timer_get_time();
    return ESP_OK;
}

esp_err_t Hx711::prepareForSleep() {
    ESP_RETURN_ON_ERROR(gpio_set_level(sck_, 1), "hx711", "PD_SCK high");
    esp_rom_delay_us(100);
    return ESP_OK;
}

esp_err_t Hx711::resumeFromSleep() {
    ESP_RETURN_ON_ERROR(gpio_set_level(sck_, 0), "hx711", "PD_SCK low");
    esp_rom_delay_us(100);
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

Hx711PollResult Hx711::poll(int32_t &value) {
    constexpr int64_t kStaleConversionUs = 350000;
    const int64_t now_us = esp_timer_get_time();
    if (gpio_get_level(dout_) != 0) {
        const int64_t reference_us = last_sample_us_ != 0 ? last_sample_us_ : monitoring_started_us_;
        if (reference_us != 0 && now_us - reference_us >= kStaleConversionUs &&
            (last_failure_us_ == 0 || now_us - last_failure_us_ >= kStaleConversionUs)) {
            last_failure_us_ = now_us;
            return Hx711PollResult::Failure;
        }
        return Hx711PollResult::Waiting;
    }

    if (!read(value, 2)) {
        last_failure_us_ = now_us;
        return Hx711PollResult::Failure;
    }

    const int64_t completed_us = esp_timer_get_time();
    if (rate_window_started_us_ == 0) {
        rate_window_started_us_ = completed_us;
        rate_window_samples_ = 1;
    } else {
        ++rate_window_samples_;
        const int64_t window_us = completed_us - rate_window_started_us_;
        if (window_us >= 5000000) {
            sample_rate_hz_ = static_cast<float>(rate_window_samples_ - 1U) * 1000000.0F /
                              static_cast<float>(window_us);
            rate_window_started_us_ = completed_us;
            rate_window_samples_ = 1;
        }
    }
    last_sample_us_ = completed_us;
    last_raw_counts_ = value;
    return Hx711PollResult::Sample;
}

void Hx711::observe(bool success, int32_t raw_value, float smoothing) {
    smoothing = std::clamp(smoothing, 0.0F, 0.95F);
    if (!success) {
        ++read_failures_;
        ++consecutive_failures_;
        signal_confidence_ = std::max<int8_t>(0, static_cast<int8_t>(signal_confidence_ - 1));
        ready_ = signal_confidence_ >= 6;
        return;
    }

    consecutive_failures_ = 0;
    signal_confidence_ = std::min<int8_t>(12, static_cast<int8_t>(signal_confidence_ + 3));
    ready_ = signal_confidence_ >= 6;
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

int32_t Hx711::lastRawCounts() const {
    return last_raw_counts_;
}

float Hx711::filtered() const {
    return ready_ ? filtered_ : 0.0F;
}

float Hx711::noiseEstimate() const {
    return ready_ ? noise_estimate_ : 0.0F;
}

float Hx711::sampleRateHz() const {
    return sample_rate_hz_;
}

uint32_t Hx711::readFailures() const {
    return read_failures_;
}

}  // namespace openwatts
