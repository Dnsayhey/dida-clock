#pragma once

#include <lvgl.h>

#include <cstdint>

// QWeather 天气图标（40x40，A4 掩码）。
//
// 数据由 scripts/gen_weather_icons.py 生成：QWeather Icons v1.8.0 的 70 个
// 天气代码，40x40，4 bit alpha。图标只带透明度，页面用 image_recolor 上色，
// 一套素材覆盖深色/浅色主题。
//
// 授权：图标 © QWeather，CC BY 4.0；来源与改动见
// components/assets/icons/NOTICE.txt。

extern "C" const lv_image_dsc_t* const dida_weather_icons[];
extern "C" const uint16_t dida_weather_icon_codes[];
extern "C" const unsigned dida_weather_icon_count;

namespace assets {

// 按和风天气的 icon 代码（"305"/"150"/"999" 等）查图标。
// 代码非法、超出范围或不在表里时返回 nullptr，调用方回退到文字。
inline const lv_image_dsc_t* WeatherIcon(const char* code) {
  if (code == nullptr || code[0] == '\0') {
    return nullptr;
  }
  unsigned value = 0;
  for (const char* p = code; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      return nullptr;
    }
    value = value * 10 + static_cast<unsigned>(*p - '0');
    if (value > 999) {
      return nullptr;
    }
  }
  for (unsigned i = 0; i < dida_weather_icon_count; ++i) {
    if (dida_weather_icon_codes[i] == value) {
      return dida_weather_icons[i];
    }
  }
  return nullptr;
}

}  // namespace assets
