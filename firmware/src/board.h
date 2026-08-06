#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

namespace openwatts::board {

inline constexpr gpio_num_t kUartTx = GPIO_NUM_21;
inline constexpr gpio_num_t kUartRx = GPIO_NUM_20;
inline constexpr gpio_num_t kUsbDp = GPIO_NUM_19;
inline constexpr gpio_num_t kUsbDm = GPIO_NUM_18;
inline constexpr gpio_num_t kUsbPresent = GPIO_NUM_8;
inline constexpr gpio_num_t kBoot = GPIO_NUM_9;
inline constexpr gpio_num_t kHx711Dout = GPIO_NUM_0;
inline constexpr gpio_num_t kHx711Sck = GPIO_NUM_1;
inline constexpr gpio_num_t kI2cSda = GPIO_NUM_6;
inline constexpr gpio_num_t kI2cScl = GPIO_NUM_7;
inline constexpr gpio_num_t kImuInt = GPIO_NUM_10;
inline constexpr gpio_num_t kGreenLed = GPIO_NUM_3;
inline constexpr gpio_num_t kBatteryAdc = GPIO_NUM_4;
inline constexpr gpio_num_t kChargeStat = GPIO_NUM_5;

static_assert(kUartTx == GPIO_NUM_21);
static_assert(kUartRx == GPIO_NUM_20);
static_assert(kUsbDp == GPIO_NUM_19);
static_assert(kUsbDm == GPIO_NUM_18);
static_assert(kImuInt == GPIO_NUM_10);
static_assert(kUsbPresent == GPIO_NUM_8);
static_assert(kBoot == GPIO_NUM_9);
static_assert(kI2cScl == GPIO_NUM_7);
static_assert(kI2cSda == GPIO_NUM_6);
static_assert(kChargeStat == GPIO_NUM_5);
static_assert(kBatteryAdc == GPIO_NUM_4);
static_assert(kGreenLed == GPIO_NUM_3);
static_assert(kHx711Sck == GPIO_NUM_1);
static_assert(kHx711Dout == GPIO_NUM_0);

inline constexpr int kI2cPort = 0;
inline constexpr uint32_t kI2cClockHz = 400000;

esp_err_t initPins();
esp_err_t initI2c();
i2c_master_bus_handle_t i2cBus();
esp_err_t initBatteryAdc();
uint32_t readBatteryMillivolts();
bool usbPresent();
bool chargeStatActive();
bool bootButtonPressed();
bool imuInterruptActive();
void setGreenLed(bool on);

}  // namespace openwatts::board
