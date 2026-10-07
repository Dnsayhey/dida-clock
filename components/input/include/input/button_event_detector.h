#pragma once

#include <cstdint>

#include "input/button_event.h"

namespace input {

// 时间参数默认值：
//   * debounce 10ms  —— 足够滤掉机械抖动的毛刺，又不至于让单击明显延迟
//   * long press 2s  —— 长按是"明确意图"的操作，阈值取长以免误触
//   * double click 600ms —— 见下方字段说明（不是常见的 400ms）
struct ButtonTiming {
  uint32_t debounce_ms = 10;
  uint32_t long_press_ms = 2000;
  // 600ms 而非常见的 400ms：单按键设备上"双击被判成单击"会跳到错误的页面，
  // 这个误判比单击慢 200ms 更糟。不要照抄通用默认值。
  uint32_t double_click_ms = 600;
};

// 按键识别状态机 —— **纯逻辑**：喂入"原始电平 + 时间戳"，产出事件。
//
// 不碰 GPIO、不碰中断、不碰平台时间，因此可以在主机上把点击/双击/长按
// 的时序全部测出来（这类时序逻辑出错时上机极难排查）。
//
// 事件语义（这四条都是判定的依据，改动会直接改变用户手感）：
//   * **长按在跨过阈值的时刻触发一次**，而不是松手时才触发；
//   * 长按之后松手**不再**产生单击；
//   * 双击窗口内的第二次按下即判为双击；
//   * 双击窗口超时且无第二次按下 -> 单击。
class ButtonEventDetector {
 public:
  explicit ButtonEventDetector(ButtonTiming timing = ButtonTiming{});

  // 由调用方以固定周期（建议 10ms）调用。
  // raw_pressed：**未消抖**的原始电平，按下为 true。
  ButtonEvent Update(bool raw_pressed, uint32_t now_ms);

  // 诊断用
  bool IsPressed() const { return stable_pressed_; }

 private:
  enum class State {
    kIdle,
    kPressed,
    kWaitingSecondClick,
    kLongFired,
  };

  ButtonTiming timing_;
  State state_ = State::kIdle;

  // 消抖：电平变化后需稳定 debounce_ms 才被接受
  bool stable_pressed_ = false;
  bool debounce_active_ = false;
  bool candidate_pressed_ = false;
  uint32_t candidate_since_ms_ = 0;

  uint32_t press_started_ms_ = 0;
  uint32_t release_ms_ = 0;
  bool long_fired_ = false;
};

}  // namespace input
