// 导航决策与命令执行的单测（主机运行）。
//
// 按钮交互规则在这里被逐格验证 —— 这是"页面行为符合设计"
// 最容易出错的地方。
//
// AppCommandExecutor 通过 app::PageHost 接口工作，因此用一个假宿主即可测试，
// 不需要 LVGL。

#include <vector>

#include "app/app_command_executor.h"
#include "app/app_controller.h"
#include "app/app_events.h"
#include "app/page_host.h"
#include "test_framework.h"

using app::AppCommand;
using app::AppCommandExecutor;
using app::AppCommandType;
using app::AppController;
using app::ButtonEvent;
using app::PageHost;
using app::PageType;

namespace {

// 记录调用轨迹的假页面宿主
class FakePageHost : public PageHost {
 public:
  void SwitchTo(PageType page) override {
    switched.push_back(page);
    current = page;
  }
  void DispatchClickToCurrent() override { ++clicks; }
  void DispatchLongPressToCurrent() override { ++long_presses; }
  PageType CurrentPage() const override { return current; }

  std::vector<PageType> switched;
  int clicks = 0;
  int long_presses = 0;
  PageType current = PageType::kInit;
};

// 断言"某个按键事件在某页上产出某个命令"
void ExpectCommand(PageType page, ButtonEvent event, AppCommandType type,
                   PageType target) {
  const AppController controller;
  const AppCommand cmd = controller.HandleButtonEvent(event, page);
  CHECK_EQ(static_cast<int>(cmd.type), static_cast<int>(type));
  if (type == AppCommandType::kSwitchPage) {
    CHECK_EQ(static_cast<int>(cmd.target_page), static_cast<int>(target));
  }
}

}  // namespace

// ---------------- 单击 ----------------

// 实时天气页 <-> 未来天气页 互切
TEST_CASE(Nav_单击在天气两页间互切) {
  ExpectCommand(PageType::kRealTimeWeather, ButtonEvent::kClick,
                AppCommandType::kSwitchPage, PageType::kFutureWeather);
  ExpectCommand(PageType::kFutureWeather, ButtonEvent::kClick,
                AppCommandType::kSwitchPage, PageType::kRealTimeWeather);
}

// 其余页面的单击没有换页语义 —— 命令是"交给当前页自己处理"。
// 表格里写"无"的页面（INIT / NETWORK_SETUP / FACTORY_RESET）由页面自身忽略。
TEST_CASE(Nav_无换页语义的页面把单击交给页面自身) {
  for (const PageType page :
       {PageType::kThemeSettings, PageType::kFactoryReset}) {
    ExpectCommand(page, ButtonEvent::kClick, AppCommandType::kDispatchPageClick,
                  page);
  }
}

// ---------------- 双击 ----------------

TEST_CASE(Nav_天气页双击进入亮度与主题页) {
  ExpectCommand(PageType::kRealTimeWeather, ButtonEvent::kDoubleClick,
                AppCommandType::kSwitchPage, PageType::kThemeSettings);
  ExpectCommand(PageType::kFutureWeather, ButtonEvent::kDoubleClick,
                AppCommandType::kSwitchPage, PageType::kThemeSettings);
}

TEST_CASE(Nav_主题页双击进入恢复出厂页) {
  ExpectCommand(PageType::kThemeSettings, ButtonEvent::kDoubleClick,
                AppCommandType::kSwitchPage, PageType::kFactoryReset);
}

TEST_CASE(Nav_恢复出厂页双击回到实时天气页) {
  ExpectCommand(PageType::kFactoryReset, ButtonEvent::kDoubleClick,
                AppCommandType::kSwitchPage, PageType::kRealTimeWeather);
}

// 闸门页：单击/双击/长按全部无响应。
//
// 这两页是启动闸门，不能被切走或跳过 —— 它们何时离开完全由启动流程决定，
// 配网失败时用户也不会误打误撞跳走。
TEST_CASE(Nav_闸门页不响应任何按键) {
  for (const PageType page : {PageType::kInit, PageType::kNetworkSetup}) {
    for (const ButtonEvent event :
         {ButtonEvent::kClick, ButtonEvent::kDoubleClick,
          ButtonEvent::kLongPress}) {
      ExpectCommand(page, event, AppCommandType::kNone, page);
    }
  }
}

// ---------------- 长按 ----------------

// 长按一律交给页面自身：
// 主题页 = 切浅色/深色；恢复出厂页 = 清 NVS 并重启；天气页没有覆写 = 无操作
TEST_CASE(Nav_其余页面长按交给页面自身) {
  for (const PageType page :
       {PageType::kFutureWeather, PageType::kThemeSettings,
        PageType::kFactoryReset}) {
    ExpectCommand(page, ButtonEvent::kLongPress,
                  AppCommandType::kDispatchPageLongPress, page);
  }
}

// 穷举性质检查：**任何页面 × 任何按键都不能切到闸门页**。
//
// 闸门页不响应按键，也没有任何按键路径能回到它们 —— 否则用户会被
// 永久困住（启动流程早已走完，闸门页上按什么都没反应）。
// 这条检查就是为"某个页面能跳回闸门页"那类陷阱准备的。
TEST_CASE(Nav_不存在任何通往闸门页的按键路径) {
  const AppController controller;
  for (int p = 0; p < static_cast<int>(PageType::kCount); ++p) {
    const PageType page = static_cast<PageType>(p);
    for (const ButtonEvent event :
         {ButtonEvent::kClick, ButtonEvent::kDoubleClick,
          ButtonEvent::kLongPress}) {
      const AppCommand cmd = controller.HandleButtonEvent(event, page);
      if (cmd.type == AppCommandType::kSwitchPage) {
        CHECK(cmd.target_page != PageType::kInit);
        CHECK(cmd.target_page != PageType::kNetworkSetup);
      }
    }
  }
}

// ---------------- 命令执行 ----------------

TEST_CASE(Exec_切页命令调用SwitchTo) {
  const AppCommandExecutor executor;
  FakePageHost host;

  executor.Execute(AppCommand::SwitchPage(PageType::kThemeSettings), host);

  CHECK_EQ(host.switched.size(), 1u);
  CHECK_EQ(static_cast<int>(host.switched[0]),
           static_cast<int>(PageType::kThemeSettings));
  CHECK_EQ(host.clicks, 0);
  CHECK_EQ(host.long_presses, 0);
}

TEST_CASE(Exec_点击命令交给当前页) {
  const AppCommandExecutor executor;
  FakePageHost host;

  executor.Execute(AppCommand::DispatchPageClick(PageType::kThemeSettings),
                   host);

  CHECK_EQ(host.clicks, 1);
  CHECK(host.switched.empty());
}

TEST_CASE(Exec_长按命令交给当前页) {
  const AppCommandExecutor executor;
  FakePageHost host;

  executor.Execute(AppCommand::DispatchPageLongPress(PageType::kFactoryReset),
                   host);

  CHECK_EQ(host.long_presses, 1);
  CHECK(host.switched.empty());
}

TEST_CASE(Exec_空命令不做任何事) {
  const AppCommandExecutor executor;
  FakePageHost host;

  executor.Execute(AppCommand::None(), host);

  CHECK(host.switched.empty());
  CHECK_EQ(host.clicks, 0);
  CHECK_EQ(host.long_presses, 0);
}

// ---------------- 端到端：按键 -> 命令 -> 页面动作 ----------------

// 完整链路：真实天气页双击应最终把宿主的当前页切到主题页
TEST_CASE(Nav端到端_天气页双击最终切到主题页) {
  const AppController controller;
  const AppCommandExecutor executor;
  FakePageHost host;
  host.current = PageType::kRealTimeWeather;

  const AppCommand cmd = controller.HandleButtonEvent(ButtonEvent::kDoubleClick,
                                                      host.CurrentPage());
  executor.Execute(cmd, host);

  CHECK_EQ(static_cast<int>(host.CurrentPage()),
           static_cast<int>(PageType::kThemeSettings));
}

// 三次双击应走完 天气 -> 主题 -> 恢复出厂 -> 天气 的环
TEST_CASE(Nav端到端_双击环游一圈回到原页) {
  const AppController controller;
  const AppCommandExecutor executor;
  FakePageHost host;
  host.current = PageType::kRealTimeWeather;

  for (int i = 0; i < 3; ++i) {
    const AppCommand cmd = controller.HandleButtonEvent(
        ButtonEvent::kDoubleClick, host.CurrentPage());
    executor.Execute(cmd, host);
  }

  CHECK_EQ(static_cast<int>(host.CurrentPage()),
           static_cast<int>(PageType::kRealTimeWeather));
}

// 天气页长按："交给页面自身"处理，而天气页没有覆写 OnLongPress，
// 因此是一次无操作 —— 关键是**不能换页**。
TEST_CASE(Nav端到端_天气页长按不换页) {
  const AppController controller;
  const AppCommandExecutor executor;
  FakePageHost host;
  host.current = PageType::kRealTimeWeather;

  const AppCommand cmd =
      controller.HandleButtonEvent(ButtonEvent::kLongPress, host.CurrentPage());
  executor.Execute(cmd, host);
  CHECK_EQ(static_cast<int>(host.CurrentPage()),
           static_cast<int>(PageType::kRealTimeWeather));
}

// 闸门页上按任何键都不出页（配网失败时用户不会误打误撞跳走）
TEST_CASE(Nav端到端_闸门页上按键不改变当前页) {
  const AppController controller;
  const AppCommandExecutor executor;

  for (const PageType gate : {PageType::kInit, PageType::kNetworkSetup}) {
    FakePageHost host;
    host.current = gate;
    for (const ButtonEvent event :
         {ButtonEvent::kClick, ButtonEvent::kDoubleClick,
          ButtonEvent::kLongPress}) {
      const AppCommand cmd =
          controller.HandleButtonEvent(event, host.CurrentPage());
      executor.Execute(cmd, host);
      CHECK_EQ(static_cast<int>(host.CurrentPage()), static_cast<int>(gate));
    }
    CHECK(host.switched.empty());
  }
}

// ---------------- 应用状态机 ----------------

TEST_CASE(Nav_应用状态流转) {
  AppController controller;
  CHECK(controller.state() == app::AppState::kIdle);

  controller.HandleWifiConfigMissing();
  CHECK(controller.state() == app::AppState::kWaitingForWifiConfig);

  controller.HandleWifiConnecting();
  CHECK(controller.state() == app::AppState::kConnectingWifi);

  controller.HandleDataSyncing();
  CHECK(controller.state() == app::AppState::kSyncingData);

  controller.HandleWeatherReady();
  CHECK(controller.state() == app::AppState::kShowingWeather);
}
