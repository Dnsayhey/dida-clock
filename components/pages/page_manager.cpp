#include "pages/page_manager.h"

#include <esp_log.h>

#include "pages/ui_theme.h"

namespace pages {
namespace {

constexpr char kTag[] = "pages";

}  // namespace

void PageManager::Register(app::PageType type, Page* page) {
  if (page == nullptr || Find(type) != nullptr) {
    return;
  }
  entries_.push_back({type, page});
}

Page* PageManager::Find(app::PageType type) const {
  for (const Entry& entry : entries_) {
    if (entry.type == type) {
      return entry.page;
    }
  }
  return nullptr;
}

bool PageManager::CreateAll(app::PageType initial) {
  if (initialized_) {
    return true;
  }

  // 先把所有页面创建出来（都处于隐藏态），再显示初始页。
  // 这样切页时只需切换可见性，不必反复创建/销毁 widget。
  for (const Entry& entry : entries_) {
    if (!entry.page->IsCreated()) {
      entry.page->Create();
    }
    HidePage(entry.page);
  }

  initialized_ = true;

  if (Find(initial) == nullptr) {
    return false;  // 初始页没有实现
  }
  current_ = initial;
  ShowPage(Find(initial));
  ESP_LOGI(kTag, "显示初始页: %s", app::PageTypeName(initial));
  return true;
}

void PageManager::HidePage(Page* page) {
  if (page == nullptr || page->Root() == nullptr) {
    return;
  }
  page->OnLeave();  // 先让页面停掉自己的 timer
  lv_obj_add_flag(page->Root(), LV_OBJ_FLAG_HIDDEN);
}

void PageManager::ShowPage(Page* page) {
  if (page == nullptr || page->Root() == nullptr) {
    return;
  }
  lv_obj_remove_flag(page->Root(), LV_OBJ_FLAG_HIDDEN);
  page->OnEnter();
}

void PageManager::SwitchTo(app::PageType page) {
  if (!initialized_ || page == current_) {
    return;
  }
  Page* target = Find(page);
  if (target == nullptr) {
    return;  // 目标页未注册，保持当前页
  }

  const app::PageType from = current_;
  HidePage(Find(current_));
  current_ = page;
  ShowPage(target);

  // 日志打在**真正切页的地方**，而不是各个调用点 ——
  // 这样启动流程、按键、双击触发的切换记录一致，也不会被挂上调用方的
  // tag（那样切页记录会显示 [net] 之类与动作不符的前缀）。
  ESP_LOGI(kTag, "切换页面: %s -> %s", app::PageTypeName(from),
           app::PageTypeName(page));
}

void PageManager::RefreshCurrent() {
  Page* page = Find(current_);
  if (page != nullptr) {
    page->Refresh();
  }
}

void PageManager::ApplyTheme(app::ThemeMode mode) {
  SetCurrentTheme(mode);
  // 对所有页面生效，包括当前隐藏的 —— 否则切回隐藏过的页面会看到旧配色
  for (const Entry& entry : entries_) {
    if (entry.page->IsCreated()) {
      entry.page->OnThemeChanged(mode);
    }
  }
}

void PageManager::DispatchClickToCurrent() {
  Page* page = Find(current_);
  if (page != nullptr) {
    page->OnClick();
  }
}

void PageManager::DispatchLongPressToCurrent() {
  Page* page = Find(current_);
  if (page != nullptr) {
    page->OnLongPress();
  }
}

}  // namespace pages
