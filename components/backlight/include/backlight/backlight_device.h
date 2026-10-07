#pragma once

#include <driver/gpio.h>

#include <cstdint>
#include <mutex>

namespace backlight {

// LEDC PWM 参数：5 kHz + 8 位。
//   * 5 kHz 远高于人眼能察觉的调光闪烁频率，背光不会出现可见抖动；
//   * 8 位正好给出 0..255 的占空比刻度，与亮度档位表和光敏换算的
//     uint8_t 取值范围一致，中间不需要任何换算。
constexpr int kPwmFreqHz = 5000;
constexpr int kPwmResolutionBits = 8;
constexpr uint32_t kPwmMaxDuty = (1u << kPwmResolutionBits) - 1u;  // 255

// 背光 PWM 输出（LEDC 低速模式 —— ESP32-C3 只有低速模式）。
class BacklightDevice {
 public:
  bool Begin(gpio_num_t pin);

  // 设置占空比 0..255，立即生效。
  void SetDuty(uint8_t duty);

  uint8_t Duty() const { return duty_; }
  bool Initialized() const { return initialized_; }

 private:
  // 两个任务都会调用 SetDuty：
  //   * UI 任务 —— 用户切换档位后立即生效，不等下一个自适应周期
  //   * net 任务 —— 周期任务按环境光刷新
  // 因此必须串行化，否则 set_duty/update_duty 两步可能交错出错误占空比。
  std::mutex mutex_;
  uint8_t duty_ = 0;
  bool initialized_ = false;
};

}  // namespace backlight
