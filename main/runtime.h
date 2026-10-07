#pragma once

// main/ 的跨任务共享状态与长生命周期对象。
//
// 为什么集中在一个文件：这些对象被 startup_task / startup_flow /
// ui_wiring / app_main 共同使用，必须有明确归属 —— 否则每个模块各 extern
// 一份，声明一旦失配就是链接期或运行期的隐性错误。
//
// 生命周期（CONVENTIONS §6.2）：**全部放在文件作用域**，在 app_main 里初始化，
// 之后不再析构。原因是 app_main 返回后主任务栈会被释放 —— 任何需要跨任务
// 存活的对象都不能是 app_main 的局部变量 —— 把 DisplayDevice 放在那里，
// app_main 返回后再调用它就是 use-after-free。

#include <atomic>
#include <cstdint>
#include <memory>

#include "app/app_command_executor.h"
#include "app/app_controller.h"
#include "backlight/backlight_device.h"
#include "backlight/light_sensor.h"
#include "display/display_device.h"
#include "esp_err.h"
#include "input/button_input.h"
#include "pages/factory_reset_page.h"
#include "pages/future_weather_page.h"
#include "pages/init_page.h"
#include "pages/network_setup_page.h"
#include "pages/page_manager.h"
#include "pages/real_time_weather_page.h"
#include "pages/theme_settings_page.h"
#include "portal/captive_portal.h"
#include "storage/device_config.h"
#include "storage/nvs_config_store.h"
#include "store/weather_store.h"
#include "ui_wiring.h"

namespace dida {

// ---------------- 文件作用域：跨任务存活的对象 ----------------

extern store::WeatherStore g_weather_store;
extern config::DeviceConfig g_device_config;  // app_main 里写一次，之后只读
extern display::DisplayDevice g_display;

// 页面对象集中持有。
//
// 数据回调在**构造时**绑定，因此本结构必须在这些回调声明之后定义，
// 且构造时刻 g_weather_store 等全局对象的地址已稳定（它们都是文件作用域，
// 满足这一点）。
struct Pages {
  pages::InitPage init;
  pages::NetworkSetupPage network_setup;
  pages::RealTimeWeatherPage real_time_weather{
      &BuildClockView, &BuildContentVersion, &BuildWeatherPageData};
  pages::FutureWeatherPage future_weather{&BuildForecastData};
  pages::ThemeSettingsPage theme_settings{&BuildSettingsData, &CycleBrightness,
                                          &ToggleTheme};
  pages::FactoryResetPage factory_reset{&DoFactoryReset};
};

extern std::unique_ptr<Pages> g_pages;
extern std::unique_ptr<pages::PageManager> g_page_manager;
extern app::AppController g_app_controller;
extern app::AppCommandExecutor g_command_executor;
extern input::ButtonInput g_button;

// ---------------- 背光、光敏、设置状态 ----------------

extern backlight::BacklightDevice g_backlight;
extern backlight::LightSensor g_light_sensor;
extern storage::NvsConfigStore g_config_store;
extern portal::CaptivePortal g_portal;

// 跨任务共享的三个标量：
//   * g_brightness_mode / g_theme_mode —— UI 任务改，启动任务的自适应任务读
//   * g_ambient_brightness             —— 启动任务写，UI 任务读（驱动自动亮度）
// 标量用 atomic 即可满足 CONVENTIONS §5（不得有未保护的跨任务可变状态）。
extern std::atomic<int> g_brightness_mode;
extern std::atomic<int> g_theme_mode;
extern std::atomic<uint8_t> g_ambient_brightness;

// ---------------- 共用工具 ----------------

uint64_t UptimeMs();

// NVS 初始化。分区耗尽或版本不匹配时自动 erase 后重试。
esp_err_t InitNvs();

// 按当前档位 + 环境光把占空比落到硬件。
// 固定档位忽略环境光；AUTO 档跟随环境光 —— 策略在 backlight::ResolveDuty 里。
void ApplyBacklight();

}  // namespace dida
