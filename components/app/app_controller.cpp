#include "app/app_controller.h"

namespace app {

const char* AppStateName(AppState state) {
  switch (state) {
    case AppState::kIdle:
      return "idle";
    case AppState::kWaitingForWifiConfig:
      return "waiting_for_wifi_config";
    case AppState::kConnectingWifi:
      return "connecting_wifi";
    case AppState::kSyncingData:
      return "syncing_data";
    case AppState::kShowingWeather:
      return "showing_weather";
  }
  return "unknown";
}

AppCommand AppController::HandleButtonEvent(ButtonEvent event,
                                            PageType current_page) const {
  // INIT 与 NETWORK_SETUP 是**启动闸门**：不响应任何按键。
  //
  // 它们只能由启动流程自动切走：
  //   * 启动成功         -> 实时天气页
  //   * 需要配网/配网失败 -> 配网页
  // 理由：没配网时让用户按键跳到天气页只会看到一片空白；而配网页是强制门户的
  // 落地页，跳过它设备就没有任何可用功能。
  if (current_page == PageType::kInit ||
      current_page == PageType::kNetworkSetup) {
    return AppCommand::None();
  }

  switch (event) {
    case ButtonEvent::kClick: {
      const PageType target = ClickTargetPage(current_page);
      if (target == current_page) {
        // 本页没有"单击换页"的语义，把点击交给页面自己处理
        // （例如设置页的单击是切换亮度档位）。
        return AppCommand::DispatchPageClick(current_page);
      }
      return AppCommand::SwitchPage(target);
    }

    case ButtonEvent::kDoubleClick:
      return AppCommand::SwitchPage(DoubleClickTargetPage(current_page));

    case ButtonEvent::kLongPress:
      // 长按一律交给页面自己处理，控制器不定义任何长按语义 ——
      // 页面不覆写 OnLongPress 时，长按就是空操作（天气页即如此）。
      return AppCommand::DispatchPageLongPress(current_page);

    case ButtonEvent::kNone:
      break;
  }
  return AppCommand::None();
}

PageType AppController::ClickTargetPage(PageType current_page) const {
  switch (current_page) {
    case PageType::kRealTimeWeather:
      return PageType::kFutureWeather;
    case PageType::kFutureWeather:
      return PageType::kRealTimeWeather;
    default:
      return current_page;  // 返回自身 => 交给页面处理
  }
}

PageType AppController::DoubleClickTargetPage(PageType current_page) const {
  switch (current_page) {
    case PageType::kRealTimeWeather:
    case PageType::kFutureWeather:
      return PageType::kThemeSettings;
    case PageType::kThemeSettings:
      return PageType::kFactoryReset;
    case PageType::kFactoryReset:
      return PageType::kRealTimeWeather;

    // INIT / NETWORK_SETUP -> 实时天气页
    //
    // 注意：这两页是**启动闸门**，HandleButtonEvent 对它们一律返回 None，
    // 所以当前流程走不到这里；这两条只是穷尽枚举的兜底映射。
    // 不要让闸门页变成可由按键切走 —— 配网失败时用户会误打误撞离开配网页。
    case PageType::kInit:
    case PageType::kNetworkSetup:
      return PageType::kRealTimeWeather;

    case PageType::kCount:
      break;
  }
  return current_page;
}

void AppController::HandleWifiConfigMissing() {
  TransitionTo(AppState::kWaitingForWifiConfig);
}

void AppController::HandleWifiConnecting() {
  TransitionTo(AppState::kConnectingWifi);
}

void AppController::HandleDataSyncing() {
  TransitionTo(AppState::kSyncingData);
}

void AppController::HandleWeatherReady() {
  TransitionTo(AppState::kShowingWeather);
}

void AppController::TransitionTo(AppState next_state) { state_ = next_state; }

}  // namespace app
