#include "ui_wiring.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>

#include "backlight/backlight_policy.h"
#include "net/network_status.h"
#include "net/sntp.h"
#include "pages/ui_theme.h"
#include "runtime.h"

namespace dida {
namespace {

constexpr char kTag[] = "dida";

// 按键轮询周期。10ms 与识别状态机的消抖/双击窗口精度匹配。
constexpr uint32_t kButtonPollMs = 10;

// 按键轮询定时器。
//
// **跑在 LVGL 任务里**（lv_timer 的天然位置），因此已经持有 LVGL 锁，
// 可以直接切页、改 widget，不需要再取锁。
//
// 关键性质：这个定时器与启动任务是独立调度实体，所以**网络任务正在做
// 阻塞 HTTP 时，按键依然即时响应**。
void PollButtonTimer(lv_timer_t* /*timer*/) {
  const uint32_t now_ms = static_cast<uint32_t>(UptimeMs());
  const input::ButtonEvent event = g_button.Poll(now_ms);
  if (event == input::ButtonEvent::kNone) {
    return;
  }

  const app::PageType current = g_page_manager->CurrentPage();
  ESP_LOGI(kTag, "按键 %s（当前页 %s）", input::ButtonEventName(event),
           app::PageTypeName(current));

  // 决策在 AppController，执行在 AppCommandExecutor，页面只收到语义动作
  const app::AppCommand command =
      g_app_controller.HandleButtonEvent(event, current);
  if (command.type != app::AppCommandType::kNone) {
    ESP_LOGI(kTag, "命令 %s -> %s", app::AppCommandTypeName(command.type),
             app::PageTypeName(command.target_page));
  }
  g_command_executor.Execute(command, *g_page_manager);
}

}  // namespace

// ---------------- 数据回调 ----------------

app::SettingsViewData BuildSettingsData() {
  return app::FormatSettingsView(
      static_cast<backlight::BrightnessMode>(g_brightness_mode.load()),
      static_cast<app::ThemeMode>(g_theme_mode.load()));
}

app::ClockViewData BuildClockView() {
  // 一次读齐日期、时间与秒内位置：分开读会在秒/日边界上给出互相矛盾的一组，
  // 而秒内位置正是页面用来把刷新对齐到秒边界的依据。
  const net::LocalClockReading clock = net::ReadLocalClock();
  return app::FormatClockView(clock.date, clock.time, clock.ms_into_second);
}

uint32_t BuildContentVersion() { return g_weather_store.Sequence(); }

app::WeatherPageViewData BuildWeatherPageData() {
  // 时间与日期不走这里：页面从 BuildClockView() 单独取。
  return app::FormatWeatherPage(g_weather_store.Snapshot(),
                                g_device_config.location);
}

app::ForecastViewData BuildForecastData() {
  // 日期用于判断"哪一行是今天"，以及丢掉已经过去的日子。
  return app::FormatForecast(g_weather_store.Snapshot(),
                             net::ReadLocalClock().date);
}

// ---------------- 命令处理 ----------------

// 单击：切换亮度档位。**立即**应用到硬件，不等下一个自适应周期，
// 否则用户按一下要等 3 秒才看到变化。
void CycleBrightness() {
  const auto current =
      static_cast<backlight::BrightnessMode>(g_brightness_mode.load());
  const backlight::BrightnessMode next = app::NextBrightnessMode(current);
  g_brightness_mode.store(static_cast<int>(next));

  // 只写单个键 —— 整份配置读改写会与启动任务读 Wi-Fi 配置竞争
  g_config_store.SaveBrightnessMode(static_cast<int>(next));

  ApplyBacklight();
  ESP_LOGI(kTag, "亮度档位 -> %s", backlight::BrightnessModeName(next));
  g_page_manager->RefreshCurrent();
}

// 长按：切换浅色/深色主题
void ToggleTheme() {
  const app::ThemeMode next =
      app::NextThemeMode(static_cast<app::ThemeMode>(g_theme_mode.load()));
  g_theme_mode.store(static_cast<int>(next));
  g_config_store.SaveThemeMode(static_cast<int>(next));

  // 对所有页面生效（包括隐藏中的），否则切回隐藏过的页面会看到旧配色
  g_page_manager->ApplyTheme(next);
  g_page_manager->RefreshCurrent();
  ESP_LOGI(kTag, "主题 -> %s", app::ThemeModeName(next));
}

// 长按恢复出厂页：清 NVS 并重启。
//
// 注意这里**不做 vTaskDelay**：本函数运行在 LVGL 任务里，
// CONVENTIONS §4 禁止在该任务阻塞。NVS 的 commit 是同步的，直接重启即可。
void DoFactoryReset() {
  ESP_LOGW(kTag, "执行恢复出厂设置：清除 NVS 并重启");
  const esp_err_t err = g_config_store.FactoryReset();
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "清 NVS 失败: %s，仍继续重启", esp_err_to_name(err));
  }
  esp_restart();
}

// ---------------- 页面注册与切换 ----------------

void RegisterPages() {
  g_page_manager->Register(app::PageType::kInit, &g_pages->init);
  g_page_manager->Register(app::PageType::kNetworkSetup,
                           &g_pages->network_setup);
  g_page_manager->Register(app::PageType::kRealTimeWeather,
                           &g_pages->real_time_weather);
  g_page_manager->Register(app::PageType::kFutureWeather,
                           &g_pages->future_weather);
  g_page_manager->Register(app::PageType::kThemeSettings,
                           &g_pages->theme_settings);
  g_page_manager->Register(app::PageType::kFactoryReset,
                           &g_pages->factory_reset);
}

void SwitchToPage(app::PageType page) {
  if (g_display.Lock(2000)) {
    g_page_manager->SwitchTo(page);
    g_display.Unlock();
  } else {
    ESP_LOGE(kTag, "切页 %s 失败：获取 LVGL 锁超时", app::PageTypeName(page));
  }
}

void StartButtonPolling() {
  lv_timer_create(PollButtonTimer, kButtonPollMs, nullptr);
}

}  // namespace dida
