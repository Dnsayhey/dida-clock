#include "input/button_event_detector.h"

namespace input {

ButtonEventDetector::ButtonEventDetector(ButtonTiming timing)
    : timing_(timing) {}

ButtonEvent ButtonEventDetector::Update(bool raw_pressed, uint32_t now_ms) {
  // ---- 1) 消抖：电平变化后需稳定 debounce_ms 才被接受 ----
  bool accepted_change = false;

  if (raw_pressed != stable_pressed_) {
    if (!debounce_active_ || candidate_pressed_ != raw_pressed) {
      // 新的候选电平，重新计时
      debounce_active_ = true;
      candidate_pressed_ = raw_pressed;
      candidate_since_ms_ = now_ms;
    } else if (now_ms - candidate_since_ms_ >= timing_.debounce_ms) {
      stable_pressed_ = candidate_pressed_;
      debounce_active_ = false;
      accepted_change = true;
    }
  } else {
    // 电平回到稳定值，取消消抖
    debounce_active_ = false;
  }

  // ---- 2) 电平未变化时，仍需检查"时间驱动"的两个事件 ----
  if (!accepted_change) {
    // 长按：跨过阈值的瞬间触发一次
    if (state_ == State::kPressed && !long_fired_ &&
        now_ms - press_started_ms_ >= timing_.long_press_ms) {
      long_fired_ = true;
      state_ = State::kLongFired;
      return ButtonEvent::kLongPress;
    }

    // 双击窗口超时 -> 判为单击
    if (state_ == State::kWaitingSecondClick &&
        now_ms - release_ms_ >= timing_.double_click_ms) {
      state_ = State::kIdle;
      return ButtonEvent::kClick;
    }

    return ButtonEvent::kNone;
  }

  // ---- 3) 接受了电平变化 ----
  if (stable_pressed_) {
    // 按下
    if (state_ == State::kWaitingSecondClick) {
      // 双击窗口内的第二次按下
      state_ = State::kIdle;
      long_fired_ = false;
      return ButtonEvent::kDoubleClick;
    }
    state_ = State::kPressed;
    press_started_ms_ = now_ms;
    long_fired_ = false;
    return ButtonEvent::kNone;
  }

  // 松手
  if (state_ == State::kPressed) {
    // 已经报过长按（或按住时间已超阈值）就不再多报一次单击
    if (long_fired_ || now_ms - press_started_ms_ >= timing_.long_press_ms) {
      state_ = State::kIdle;
      return ButtonEvent::kNone;
    }
    // 进入双击等待窗口
    state_ = State::kWaitingSecondClick;
    release_ms_ = now_ms;
    return ButtonEvent::kNone;
  }

  state_ = State::kIdle;
  return ButtonEvent::kNone;
}

}  // namespace input
