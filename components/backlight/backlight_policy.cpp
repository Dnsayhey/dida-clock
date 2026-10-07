#include "backlight/backlight_policy.h"

namespace backlight {
namespace {

// 整数线性映射（输入先夹取，避免中间结果溢出）
int32_t LinearMap(int32_t value, int32_t in_min, int32_t in_max,
                  int32_t out_min, int32_t out_max) {
  if (in_max == in_min) {
    return out_min;
  }
  return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

int32_t Clamp(int32_t value, int32_t low, int32_t high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

}  // namespace

const char* BrightnessModeName(BrightnessMode mode) {
  switch (mode) {
    case BrightnessMode::kAuto:
      return "auto";
    case BrightnessMode::kLevel1:
      return "level1";
    case BrightnessMode::kLevel2:
      return "level2";
    case BrightnessMode::kLevel3:
      return "level3";
    case BrightnessMode::kCount:
      break;
  }
  return "unknown";
}

uint8_t RawToBrightness(uint16_t raw, uint16_t min_value, uint16_t max_value) {
  // 先把 raw 夹到标定区间内，避免映射结果越界
  const int32_t bounded =
      Clamp(static_cast<int32_t>(raw), static_cast<int32_t>(min_value),
            static_cast<int32_t>(max_value));
  const int32_t mapped = LinearMap(bounded, static_cast<int32_t>(min_value),
                                   static_cast<int32_t>(max_value), 0, 255);
  // 取反：ADC 读数越大表示越暗，亮度值越小
  return static_cast<uint8_t>(Clamp(255 - mapped, 0, 255));
}

BrightnessMode NextMode(BrightnessMode mode) {
  const int next =
      (static_cast<int>(mode) + 1) % static_cast<int>(BrightnessMode::kCount);
  return static_cast<BrightnessMode>(next);
}

uint8_t ResolveDuty(BrightnessMode mode, uint8_t auto_brightness) {
  // 环境亮度可能被光敏读数压到 0（极暗环境），而 0 占空比就是黑屏。
  // 下限只加在这里而不是 RawToBrightness() 里：那个函数是"原始值 -> 亮度"
  // 的纯映射，加了下限后它的输出就不再是真实亮度，设置页显示的档位名也会
  // 失真。策略归策略，映射归映射。
  const uint8_t floored =
      auto_brightness < kMinAutoDuty ? kMinAutoDuty : auto_brightness;
  if (mode == BrightnessMode::kAuto) {
    return floored;
  }
  if (mode == BrightnessMode::kCount) {
    return floored;
  }
  return kFixedDuty[static_cast<int>(mode)];
}

}  // namespace backlight
