#include "led_status.h"

#include <cmath>

#include "board.h"
#include "driver/ledc.h"

namespace openwatts {
namespace {
constexpr uint32_t kMaximumDuty = 255;
constexpr int64_t kBootDurationUs = 720000;

bool inRange(uint32_t value, uint32_t start, uint32_t end) {
    return value >= start && value < end;
}
}

LedStatusController &ledStatus() {
    static LedStatusController controller;
    return controller;
}

esp_err_t LedStatusController::begin() {
    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_8_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;

    ledc_channel_config_t channel{};
    channel.gpio_num = board::kGreenLed;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.intr_type = LEDC_INTR_DISABLE;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = 0;
    channel.hpoint = 0;
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) return err;

    boot_started_us_ = esp_timer_get_time();
    pattern_started_us_ = boot_started_us_;
    rendered_ = LedPattern::Boot;
    esp_timer_create_args_t args{.callback = &LedStatusController::timerCallback,
                                 .arg = this,
                                 .dispatch_method = ESP_TIMER_TASK,
                                 .name = "status_led",
                                 .skip_unhandled_events = true};
    err = esp_timer_create(&args, &timer_);
    if (err != ESP_OK) return err;
    return esp_timer_start_periodic(timer_, 20000);
}

void LedStatusController::setAutomaticState(bool usb_present, bool maintenance, bool ble_connected,
                                            bool calibration_required, bool fatal_error) {
    LedPattern pattern = LedPattern::Breathing;
    if (fatal_error) pattern = LedPattern::FatalSos;
    else if (calibration_required) pattern = LedPattern::CalibrationRequired;
    else if (usb_present || maintenance) pattern = LedPattern::Solid;
    else if (ble_connected) pattern = LedPattern::Off;
    automatic_.store(pattern);
}

void LedStatusController::setSleeping(bool sleeping) { sleeping_.store(sleeping); }
void LedStatusController::setOtaActive(bool active) { ota_active_.store(active); }

LedPattern LedStatusController::effectivePattern() const {
    if (sleeping_.load()) return LedPattern::Off;
    if (ota_active_.load()) return LedPattern::Ota;
    if (esp_timer_get_time() - boot_started_us_ < kBootDurationUs) return LedPattern::Boot;
    return automatic_.load();
}

void LedStatusController::timerCallback(void *arg) {
    static_cast<LedStatusController *>(arg)->updateOutput();
}

void LedStatusController::updateOutput() {
    const LedPattern pattern = effectivePattern();
    const int64_t now_us = esp_timer_get_time();
    if (pattern != rendered_) {
        rendered_ = pattern;
        pattern_started_us_ = now_us;
    }
    const uint32_t elapsed_ms = static_cast<uint32_t>((now_us - pattern_started_us_) / 1000);
    uint32_t duty = 0;
    switch (pattern) {
        case LedPattern::Off:
            break;
        case LedPattern::Solid:
            duty = kMaximumDuty;
            break;
        case LedPattern::Breathing: {
            constexpr float kPi = 3.14159265358979323846F;
            const float phase = static_cast<float>(elapsed_ms % 4000U) / 4000.0F * 2.0F * kPi;
            const float level = (1.0F - std::cos(phase)) * 0.5F;
            duty = static_cast<uint32_t>(8.0F + level * level * 247.0F);
            break;
        }
        case LedPattern::Boot: {
            const uint32_t t = elapsed_ms % 720U;
            duty = (inRange(t, 0, 100) || inRange(t, 220, 320) || inRange(t, 440, 540)) ? kMaximumDuty : 0;
            break;
        }
        case LedPattern::Ota:
            duty = ((elapsed_ms / 100U) % 2U) == 0 ? kMaximumDuty : 0;
            break;
        case LedPattern::CalibrationRequired: {
            const uint32_t t = elapsed_ms % 2000U;
            duty = (inRange(t, 0, 140) || inRange(t, 280, 420)) ? kMaximumDuty : 0;
            break;
        }
        case LedPattern::FatalSos: {
            const uint32_t t = elapsed_ms % 4200U;
            duty = (inRange(t, 0, 120) || inRange(t, 240, 360) || inRange(t, 480, 600) ||
                    inRange(t, 960, 1320) || inRange(t, 1440, 1800) || inRange(t, 1920, 2280) ||
                    inRange(t, 2640, 2760) || inRange(t, 2880, 3000) || inRange(t, 3120, 3240))
                       ? kMaximumDuty : 0;
            break;
        }
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

}  // namespace openwatts
