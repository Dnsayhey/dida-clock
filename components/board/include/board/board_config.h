#pragma once

// 板级硬件事实的唯一来源：引脚、外设通道、屏幕参数。
//
// **本组件只描述不可变的硬件事实，不含策略与逻辑。**
// 因此它是 header-only —— 一旦某天这里出现了 .cpp，说明有东西放错了层。
// 例如"背光 PWM 频率""档位表""光敏换算曲线"都属于策略，
// 放在 backlight 组件里；本文件只声明"背光接在 GPIO6""光敏接在 GPIO0/ADC1_CH0"。

#include <driver/gpio.h>
#include <hal/adc_types.h>

#include <cstdint>

namespace board {

// ---------------- 显示：240x320 ST7789 (SPI) ----------------
constexpr int kLcdHRes = 240;
constexpr int kLcdVRes = 320;

constexpr gpio_num_t kLcdSclk = GPIO_NUM_2;  // TFT_SCLK
constexpr gpio_num_t kLcdMosi = GPIO_NUM_3;  // TFT_MOSI
constexpr gpio_num_t kLcdDc = GPIO_NUM_4;    // TFT_DC
constexpr gpio_num_t kLcdRst = GPIO_NUM_5;   // TFT_RST
constexpr gpio_num_t kLcdCs = GPIO_NUM_7;    // TFT_CS

constexpr int kLcdPixelClockHz = 27 * 1000 * 1000;

// 面板元素顺序。这是本面板在 esp_lcd + LVGL 下唯一正确的设置，不要照搬其它显示库
// 对字节序的处理：RGB565 是 5-6-5 打包，字段不按字节对齐，"字节序"与"BGR 位"会
// 互相影响。四色块实测四种组合，只有 swap_bytes=1 + BGR=0 能得到红/绿/蓝。
constexpr bool kLcdBgrElementOrder = false;

constexpr bool kLcdInvertColor = false;

// ---------------- 背光与光敏 ----------------
constexpr gpio_num_t kBacklightPin = GPIO_NUM_6;

// 光敏接在 GPIO0。ESP32-C3 上 GPIO0 即 ADC1 通道 0。
constexpr gpio_num_t kLightSensorPin = GPIO_NUM_0;
constexpr adc_unit_t kLightSensorAdcUnit = ADC_UNIT_1;
constexpr adc_channel_t kLightSensorAdcChannel = ADC_CHANNEL_0;

// ---------------- 其他功能引脚 ----------------
constexpr gpio_num_t kButtonPin = GPIO_NUM_8;
constexpr gpio_num_t kStatusLedPin = GPIO_NUM_12;

}  // namespace board
