#pragma once

#include <driver/gpio.h>

#include <cstdint>

#include "input/button_event_detector.h"

namespace input {

// 设备侧按键输入：GPIO 采样 + 识别状态机。
//
// 刻意**不**在这里创建定时器、也不依赖 LVGL —— 由装配层决定以什么节奏、
// 在哪个任务里轮询。本类只依赖 gpio 驱动。
//
// 装配层的选择是"在 LVGL 任务里用 lv_timer 轮询"，理由有两条：
//   1. 按键最终要改 UI，而 UI 只能在 LVGL 任务里改（已持 LVGL 锁）；
//   2. **网络任务做阻塞 I/O 时按键依然即时响应**。
class ButtonInput {
 public:
  bool Begin(gpio_num_t pin, ButtonTiming timing = ButtonTiming{});

  // 由调用方按固定周期（建议 10ms）调用。
  ButtonEvent Poll(uint32_t now_ms);

  bool IsPressed() const { return detector_.IsPressed(); }

 private:
  gpio_num_t pin_ = GPIO_NUM_NC;
  ButtonEventDetector detector_;
  bool initialized_ = false;
};

}  // namespace input
