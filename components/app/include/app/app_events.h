#pragma once

#include "app/page_type.h"
#include "input/button_event.h"

namespace app {

// 按键事件类型由输入层定义（input::ButtonEvent），应用层只消费它。
// 这里导出别名，让应用层代码读起来更自然。
using ButtonEvent = input::ButtonEvent;

// 应用层决策产出的命令。页面切换的**决策**在 AppController，
// **执行**在 AppCommandExecutor —— 页面本身不处理原始按键事件。
enum class AppCommandType {
  kNone,
  kSwitchPage,
  kDispatchPageClick,
  kDispatchPageLongPress,
};

struct AppCommand {
  AppCommandType type = AppCommandType::kNone;
  PageType target_page = PageType::kInit;

  AppCommand() = default;
  AppCommand(AppCommandType command_type, PageType page)
      : type(command_type), target_page(page) {}

  static AppCommand None() { return {}; }
  static AppCommand SwitchPage(PageType page) {
    return {AppCommandType::kSwitchPage, page};
  }
  static AppCommand DispatchPageClick(PageType page) {
    return {AppCommandType::kDispatchPageClick, page};
  }
  static AppCommand DispatchPageLongPress(PageType page) {
    return {AppCommandType::kDispatchPageLongPress, page};
  }
};

const char* AppCommandTypeName(AppCommandType type);

}  // namespace app
