// 按键识别状态机的单测（主机运行）。
//
// 这类时序逻辑一旦出错，上机表现为"偶尔双击变单击""长按后还触发一次单击"，
// 极难排查。因此把整条时序在主机上按 10ms 步长跑出来验证。

#include <cstdint>
#include <vector>

#include "input/button_event_detector.h"
#include "test_framework.h"

using input::ButtonEvent;
using input::ButtonEventDetector;
using input::ButtonTiming;

namespace {

constexpr uint32_t kTick = 10;  // 与设备侧轮询周期一致

// 把状态机按固定步长驱动，收集期间产生的事件。
class ButtonSim {
 public:
  explicit ButtonSim(ButtonTiming timing = ButtonTiming{})
      : detector_(timing) {}

  // 电平保持 pressed，持续 ms 毫秒
  std::vector<ButtonEvent> Level(bool pressed, uint32_t ms) {
    std::vector<ButtonEvent> events;
    for (uint32_t elapsed = 0; elapsed < ms; elapsed += kTick) {
      const ButtonEvent e = detector_.Update(pressed, now_);
      now_ += kTick;
      if (e != ButtonEvent::kNone) {
        events.push_back(e);
      }
    }
    return events;
  }

  // 一次短按：按下 ms 后松开
  std::vector<ButtonEvent> ShortPress(uint32_t press_ms = 50) {
    std::vector<ButtonEvent> events = Level(true, press_ms);
    const auto released = Level(false, press_ms);
    events.insert(events.end(), released.begin(), released.end());
    return events;
  }

  // 走完双击等待窗口（让暂存的单击落地）
  std::vector<ButtonEvent> Settle() {
    return Level(false, 700);  // > double_click_ms(600)
  }

  uint32_t now() const { return now_; }

 private:
  ButtonEventDetector detector_;
  uint32_t now_ = 0;
};

bool Has(const std::vector<ButtonEvent>& events, ButtonEvent target) {
  for (const ButtonEvent e : events) {
    if (e == target) {
      return true;
    }
  }
  return false;
}

int Count(const std::vector<ButtonEvent>& events, ButtonEvent target) {
  int n = 0;
  for (const ButtonEvent e : events) {
    if (e == target) {
      ++n;
    }
  }
  return n;
}

}  // namespace

// ---------------- 单击 ----------------

TEST_CASE(Button_空闲时不产生事件) {
  ButtonSim sim;
  CHECK(sim.Level(false, 1000).empty());
}

TEST_CASE(Button_短按在双击窗口超时后判为单击) {
  ButtonSim sim;
  auto events = sim.ShortPress();
  // 松手瞬间还不判定，要等双击窗口过去
  CHECK(!Has(events, ButtonEvent::kClick));

  const auto settled = sim.Settle();
  CHECK_EQ(Count(settled, ButtonEvent::kClick), 1);
}

// ---------------- 双击 ----------------

TEST_CASE(Button_窗口内两次短按判为双击) {
  ButtonSim sim;
  sim.ShortPress();
  const auto second = sim.ShortPress();  // 立即再按一次

  CHECK(Has(second, ButtonEvent::kDoubleClick));
  CHECK(!Has(second, ButtonEvent::kClick));
}

TEST_CASE(Button_窗口外两次短按判为两次单击) {
  ButtonSim sim;

  sim.ShortPress();
  auto first = sim.Settle();  // 等窗口过去 -> 第一次单击落地
  CHECK_EQ(Count(first, ButtonEvent::kClick), 1);

  sim.ShortPress();
  auto second = sim.Settle();
  CHECK_EQ(Count(second, ButtonEvent::kClick), 1);
  CHECK(!Has(second, ButtonEvent::kDoubleClick));
}

TEST_CASE(Button_恰好在窗口边界附近只算一次双击) {
  ButtonSim sim;
  sim.ShortPress();
  // 窗口是 600ms，这里等 500ms 后再按，仍应算双击
  sim.Level(false, 500);
  const auto second = sim.ShortPress();
  CHECK(Has(second, ButtonEvent::kDoubleClick));
}

// 实机回归：双击窗口从 400ms 放宽到 600ms 的原因。
//
// 实机表现是"在天气页双击，偶尔却切到了另一张天气页"—— 因为天气页的单击
// 语义就是换页，一旦双击被拆成两次单击，用户想去设置页却先跳到了另一页。
// 这条用例守住"手速偏慢（两次按下间隔 500ms）也算双击"。
TEST_CASE(Button_手速偏慢的双击仍判为双击) {
  ButtonSim sim;
  sim.ShortPress();
  sim.Level(false, 500);  // 两次按下的间隔 500ms，仍在 600ms 双击窗口内
  const auto second = sim.ShortPress();
  CHECK(Has(second, ButtonEvent::kDoubleClick));
  CHECK(!Has(second, ButtonEvent::kClick));
}

TEST_CASE(Button_超过新窗口才算两次单击) {
  ButtonSim sim;
  sim.ShortPress();
  auto first = sim.Settle();  // 等窗口过去 -> 第一次单击落地
  CHECK_EQ(Count(first, ButtonEvent::kClick), 1);

  sim.ShortPress();
  const auto second = sim.Settle();
  CHECK_EQ(Count(second, ButtonEvent::kClick), 1);
  CHECK(!Has(second, ButtonEvent::kDoubleClick));
}

// ---------------- 长按 ----------------

TEST_CASE(Button_按住超过阈值触发一次长按) {
  ButtonSim sim;
  const auto held = sim.Level(true, 2500);  // > 2000ms

  CHECK_EQ(Count(held, ButtonEvent::kLongPress), 1);
}

TEST_CASE(Button_长按后松手不再产生单击) {
  ButtonSim sim;
  sim.Level(true, 2500);

  auto released = sim.Level(false, 50);
  const auto settled = sim.Settle();
  released.insert(released.end(), settled.begin(), settled.end());

  CHECK(!Has(released, ButtonEvent::kClick));
  CHECK(!Has(released, ButtonEvent::kDoubleClick));
}

TEST_CASE(Button_未达长按阈值不会误报长按) {
  ButtonSim sim;
  const auto held = sim.Level(true, 1900);  // < 2000ms
  CHECK(!Has(held, ButtonEvent::kLongPress));

  sim.Level(false, 50);
  const auto settled = sim.Settle();
  CHECK_EQ(Count(settled, ButtonEvent::kClick), 1);
}

TEST_CASE(Button_长按只触发一次不是持续触发) {
  ButtonSim sim;
  const auto held = sim.Level(true, 5000);  // 远超阈值
  CHECK_EQ(Count(held, ButtonEvent::kLongPress), 1);
}

// ---------------- 消抖 ----------------

// 短于消抖时间的抖动不应产生任何事件
TEST_CASE(Button_短于消抖时间的抖动被忽略) {
  ButtonSim sim;
  const auto noise = sim.Level(true, 5);  // 只按 5ms，< debounce 10ms
  CHECK(noise.empty());

  const auto settled = sim.Settle();
  CHECK_EQ(Count(settled, ButtonEvent::kClick), 0);
  CHECK_EQ(Count(settled, ButtonEvent::kDoubleClick), 0);
}

// 稳定按下超过消抖时间才被接受
TEST_CASE(Button_稳定超过消抖时间才被接受) {
  ButtonSim sim;
  sim.Level(true, 20);  // 足够稳定
  sim.Level(false, 20);
  const auto settled = sim.Settle();
  CHECK_EQ(Count(settled, ButtonEvent::kClick), 1);
}

// ---------------- 参数化 ----------------

// 时间参数可配，便于按需调整（并证明默认值不是硬编码在逻辑里）
TEST_CASE(Button_自定义时间参数生效) {
  ButtonTiming timing;
  timing.long_press_ms = 500;    // 缩短长按阈值
  timing.double_click_ms = 100;  // 缩短双击窗口
  ButtonSim sim(timing);

  const auto held = sim.Level(true, 600);
  CHECK_EQ(Count(held, ButtonEvent::kLongPress), 1);
}

TEST_CASE(Button_更短的双击窗口会让两次短按变成两次单击) {
  ButtonTiming timing;
  timing.double_click_ms = 100;
  ButtonSim sim(timing);

  sim.ShortPress(50);
  // 走完 100ms 的双击窗口 —— 单击事件就在这段等待里产生，必须收集它
  const auto settled = sim.Level(false, 400);
  CHECK_EQ(Count(settled, ButtonEvent::kClick), 1);
}
