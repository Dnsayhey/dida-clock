#pragma once

#include "app/app_events.h"
#include "app/page_host.h"

namespace app {

// 把 AppCommand 落到页面宿主上。
//
// 刻意做成纯逻辑（只调用 PageHost 接口，不碰 LVGL、不依赖 ESP-IDF），
// 因此可以用一个假的 PageHost 在主机上验证"命令 -> 页面动作"的映射。
//
// 分层意义：app 层只认识自己定义的 PageHost 端口，
// 不认识 pages::PageManager —— 这样 pages -> app 单向依赖，无循环。
// 见 CONVENTIONS.md §1.5。
class AppCommandExecutor {
 public:
  void Execute(const AppCommand& command, PageHost& host) const;
};

}  // namespace app
