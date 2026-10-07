#pragma once

#include <cstddef>
#include <vector>

#include "app/page_host.h"
#include "app/page_type.h"
#include "pages/page.h"

namespace pages {

// 页面管理器：持有全部页面、负责显示/隐藏与事件分发。
//
// 它是 app::PageHost 的实现 —— app 层只认识自己定义的端口，不认识本类，
// 这样 pages -> app 是单向依赖，不产生循环（见 CONVENTIONS.md §1.5）。
//
// **并发约定：全部方法只能在 UI 任务调用（已持 LVGL 锁）。**
class PageManager : public app::PageHost {
 public:
  // 注册页面。必须在 CreateAll() 之前、单线程阶段完成 ——
  // 与 AppScheduler 的"启动期注册"同理（CONVENTIONS.md §6.1）。
  void Register(app::PageType type, Page* page);

  // 创建全部已注册页面的 widget，并显示 initial 页。需持 LVGL 锁。
  // 返回 false 表示有页面缺少实现。
  bool CreateAll(app::PageType initial);

  // ---- app::PageHost ----
  void SwitchTo(app::PageType page) override;
  void DispatchClickToCurrent() override;
  void DispatchLongPressToCurrent() override;
  app::PageType CurrentPage() const override { return current_; }

  // 刷新当前页显示（页面内状态变化后调用）。
  void RefreshCurrent();

  // 切换主题：更新全局主题状态，并对所有已创建页面应用新配色。
  // 需持 LVGL 锁。
  void ApplyTheme(app::ThemeMode mode);

  // 诊断用
  std::size_t RegisteredCount() const { return entries_.size(); }
  bool IsInitialized() const { return initialized_; }
  bool HasPage(app::PageType type) const { return Find(type) != nullptr; }

 private:
  struct Entry {
    app::PageType type;
    Page* page;
  };

  Page* Find(app::PageType type) const;
  void HidePage(Page* page);
  void ShowPage(Page* page);

  std::vector<Entry> entries_;
  app::PageType current_ = app::PageType::kInit;
  bool initialized_ = false;
};

}  // namespace pages
