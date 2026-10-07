#pragma once

#include "app/page_type.h"

namespace app {

// 页面宿主（依赖倒置的端口）。
//
// 为什么需要这个接口：AppCommandExecutor 要把 AppCommand 落到"切页 / 把点击
// 交给当前页"，这需要调用页面管理器；但页面管理器在 pages 层，而 pages 层
// 已经依赖 app 层（页面要用 app 提供的视图数据）——直接调用就形成循环依赖。
//
// 解法：**接口定义在 app（被依赖方），实现在 pages（依赖方）**。
// app 只知道自己定义的 PageHost，不认识 pages::PageManager。
//
// 所有方法都只能在 UI 任务调用（已持 LVGL 锁），见 CONVENTIONS.md §4。
class PageHost {
 public:
  virtual ~PageHost() = default;

  virtual void SwitchTo(PageType page) = 0;
  virtual void DispatchClickToCurrent() = 0;
  virtual void DispatchLongPressToCurrent() = 0;
  virtual PageType CurrentPage() const = 0;
};

}  // namespace app
