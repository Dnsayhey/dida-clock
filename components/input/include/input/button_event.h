#pragma once

namespace input {

// 按键动作。
//
// 放在输入层而不是应用层：click / double-click / long-press 描述的
// 是"按键做了什么"，属于输入层的输出；应用层只消费它做决策。
//
// 只保留一份定义 —— 输入层与应用层各有一份相同枚举会随时间漂移。
enum class ButtonEvent {
  kNone,
  kClick,
  kDoubleClick,
  kLongPress,
};

const char* ButtonEventName(ButtonEvent event);

}  // namespace input
