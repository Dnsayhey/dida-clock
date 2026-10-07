#pragma once

#include "app/app_events.h"
#include "app/page_type.h"

namespace app {

// 应用层状态。用于日志与（后续的）启动流程分支判断。
enum class AppState {
  kIdle,
  kWaitingForWifiConfig,
  kConnectingWifi,
  kSyncingData,
  kShowingWeather,
};

const char* AppStateName(AppState state);

// 应用决策：把**按键事件**转成**命令**。
//
// 纯逻辑：不碰 GPIO、不碰 LVGL、不碰网络 —— 可在主机上单测。
// 页面切换规则集中在这里，页面自身不参与导航决策：避免"每个页面都知道
// 别的页面"，也让整套导航规则能在主机上单测。
class AppController {
 public:
  // 把按键事件与当前页转成命令。
  AppCommand HandleButtonEvent(ButtonEvent event, PageType current_page) const;

  // 单击的目标页：天气两页之间切换；其余页面返回自身，
  // 此时 AppController 会产出 kDispatchPageClick 让页面自行处理。
  PageType ClickTargetPage(PageType current_page) const;

  // 双击的目标页：天气页 -> 设置页 -> 恢复出厂页 -> 天气页。
  PageType DoubleClickTargetPage(PageType current_page) const;

  AppState state() const { return state_; }
  void HandleWifiConfigMissing();
  void HandleWifiConnecting();
  void HandleDataSyncing();
  void HandleWeatherReady();

 private:
  void TransitionTo(AppState next_state);

  AppState state_ = AppState::kIdle;
};

}  // namespace app
