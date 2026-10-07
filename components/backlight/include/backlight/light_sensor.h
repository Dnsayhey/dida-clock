#pragma once

#include <esp_adc/adc_oneshot.h>

#include <cstdint>

namespace backlight {

// 光敏传感器（ADC 单次采样）。
//
// 单次采样得到 0-4095（12 位）的原始值。
// ADC 读数越大表示越暗，亮度换算见 backlight_policy.h 的 RawToBrightness()。
class LightSensor {
 public:
  ~LightSensor();

  LightSensor() = default;
  LightSensor(const LightSensor&) = delete;
  LightSensor& operator=(const LightSensor&) = delete;

  bool Begin(adc_unit_t unit, adc_channel_t channel,
             adc_atten_t atten = ADC_ATTEN_DB_12);

  // 读原始值。失败时返回 false 且不改动 out_raw。
  bool ReadRaw(uint16_t& out_raw) const;

  bool Initialized() const { return handle_ != nullptr; }

 private:
  adc_oneshot_unit_handle_t handle_ = nullptr;
  adc_channel_t channel_ = ADC_CHANNEL_0;
};

}  // namespace backlight
