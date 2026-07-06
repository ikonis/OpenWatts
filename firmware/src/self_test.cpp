#include "self_test.h"

#include <cinttypes>
#include <cstdio>

#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hx711.h"
#include "imu_lsm6ds3.h"

namespace openwatts {
namespace {
constexpr char kTag[] = "self_test";
char g_summary[192]{};
}

SelfTestResult SelfTest::run(Lsm6ds3 &imu, Hx711 &hx711) {
    SelfTestResult result{};

    for (int i = 0; i < 3; ++i) {
        board::setGreenLed(true);
        vTaskDelay(pdMS_TO_TICKS(120));
        board::setGreenLed(false);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    result.led_tested = true;

    result.usb_present = board::usbPresent();
    result.charge_active = board::chargeStatActive();
    result.battery_mv = board::readBatteryMillivolts();

    result.imu_who_am_i = imu.whoAmI();
    result.imu_detected = result.imu_who_am_i == 0x69;

    int32_t raw = 0;
    result.hx711_ready = hx711.read(raw, 150);
    result.hx711_raw = raw;

    ESP_LOGI(kTag, "USB_PRESENT=%d CHG_STAT=%d battery=%" PRIu32 "mV", result.usb_present ? 1 : 0,
             result.charge_active ? 1 : 0, result.battery_mv);
    ESP_LOGI(kTag, "LSM6DS3 detected=%d WHO_AM_I=0x%02x", result.imu_detected ? 1 : 0, result.imu_who_am_i);
    ESP_LOGI(kTag, "HX711 ready=%d raw=%" PRId32, result.hx711_ready ? 1 : 0, result.hx711_raw);
    ESP_LOGI(kTag, "LED tested=%d", result.led_tested ? 1 : 0);
    return result;
}

const char *SelfTest::summary(const SelfTestResult &result) {
    snprintf(g_summary, sizeof(g_summary), "imu=%d who=0x%02x hx=%d raw=%" PRId32 " batt=%" PRIu32
                                           " usb=%d chg=%d led=%d",
             result.imu_detected ? 1 : 0, result.imu_who_am_i, result.hx711_ready ? 1 : 0, result.hx711_raw,
             result.battery_mv, result.usb_present ? 1 : 0, result.charge_active ? 1 : 0,
             result.led_tested ? 1 : 0);
    return g_summary;
}

}  // namespace openwatts
