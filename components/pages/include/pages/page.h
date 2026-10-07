#pragma once

#include <lvgl.h>

#include "app/settings_view_data.h"

namespace pages {

// 页面基类。
//
// **并发约定：本类所有虚函数都只能从 UI 任务调用（即已持有 LVGL 锁）。**
// 这是"UI 访问纪律"在页面层的体现 —— 任何非 UI 任务想改变页面，
// 必须通过消息投递，由 UI 任务代为执行。
//
class Page {
 public:
  virtual ~Page() = default;

  // 创建 widget。只应调用一次。调用后 Root() 可用。
  virtual void Create() = 0;

  // 进入本页：用于启动本页自己的 lv_timer。
  virtual void OnEnter() {}

  // 离开本页：用于停掉本页的 lv_timer，避免隐藏页面继续刷新。
  virtual void OnLeave() {}

  // 刷新显示内容（由本页的 lv_timer 或 PageManager 驱动）。
  virtual void Refresh() {}

  // 主题变化。PageManager 在切换主题后会对**所有**已创建页面调用它，
  // 即使该页当前隐藏 —— 否则切回隐藏过的页面会看到旧配色。
  // 默认实现只更新全屏容器的背景色；有文字颜色的页面应覆写。
  virtual void OnThemeChanged(app::ThemeMode mode);

  // 当前页收到"单击" / "长按"。
  // 由 PageManager 在 AppCommandExecutor 分发命令时调用 —— 页面**不**处理
  // 原始按键事件，只处理已经过应用层决策的语义动作。
  //
  // 只有需要页内交互的页面才覆写：
  //   亮度与主题页：OnClick 切亮度档位、OnLongPress 切浅色/深色
  //   恢复出厂页：OnLongPress 清 NVS 并重启
  virtual void OnClick() {}
  virtual void OnLongPress() {}

  const char* Name() const { return name_; }
  lv_obj_t* Root() const { return root_; }
  bool IsCreated() const { return root_ != nullptr; }

 protected:
  explicit Page(const char* name) : name_(name) {}

  // 创建本页的**全屏容器**并设为 Root，页面内容应全部加到它上面。
  //
  // 为什么不直接把 widget 建在 lv_screen_active() 上：所有页面共用同一个 screen，
  // 若各自把 widget 建在 screen 上，隐藏某一页就无从下手，多页会叠在一起。
  // 每页一个全屏容器后，PageManager 只要隐藏容器即可隐藏整页。
  lv_obj_t* CreateFullScreenRoot();

  void SetRoot(lv_obj_t* root) { root_ = root; }

 private:
  const char* name_;
  lv_obj_t* root_ = nullptr;
};

}  // namespace pages
