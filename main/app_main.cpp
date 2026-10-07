// DIDA on ESP-IDF v6.1 — 装配层。
//
// 本文件**只做装配**：按顺序初始化各子系统、绑定依赖、创建启动任务。
// 具体职责分布在 main/ 的其他文件里：
//   * runtime.{h,cpp}       跨任务状态与长生命周期对象
//   * startup_task.{h,cpp}  全部阻塞 I/O（联网/对时/天气同步 + 常驻调度循环）
//   * startup_flow.{h,cpp}  强制门户与"进入正式页面"的决策
//   * ui_wiring.{h,cpp}     页面注册、切页、数据回调、按键轮询
//
// 任务模型：
//   * LVGL 任务由 esp_lvgl_port 创建并拥有 —— 只做 UI，绝不阻塞；
//   * 启动任务 —— 承担全部阻塞 I/O：WiFi、NTP、HTTP 天气同步。
//
// 跨任务数据交接：启动任务写 store::WeatherStore / net::NetworkStatus，
// UI 任务通过 Snapshot() / GetNetworkStatus() 取拷贝。这两条路径的安全性
// 已由主机单测 + ThreadSanitizer 验证（tests/host/）。

#include <esp_app_desc.h>
#include <esp_check.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>

#include <memory>

#include "app/runtime_status.h"
#include "backlight/backlight_policy.h"
#include "board/board_config.h"
#include "dev_config.h"
#include "net/wifi_driver.h"
#include "pages/ui_theme.h"
#include "runtime.h"
#include "selftest.h"
#include "startup_task.h"
#include "ui_wiring.h"

namespace {
constexpr char kTag[] = "dida";
}  // namespace

extern "C" void app_main(void) {
  ESP_LOGI(kTag, "DIDA (ESP-IDF) 启动 | 固件 %s",
           esp_app_get_description()->version);

  ESP_ERROR_CHECK(dida::InitNvs());

  // ---- 网络栈 + WiFi 驱动：必须早于任何用网的操作 ----
  // 放在装配层而非 WifiStation::Begin()，理由见 net/wifi_driver.h。
  if (net::EnsureWifiDriverReady() != ESP_OK) {
    ESP_LOGE(kTag, "网络基础设施初始化失败，退出");
    return;
  }

  // 解压链路自检（验证 ROM miniz 的 gzip 解压确实可用）。
  // 失败不阻断启动，只打错误日志 —— 天气同步会在上层报出明确原因。
  selftest::RunDecompressSelfTest();

  // 先读配置：启动任务要用位置
  app::GetStartupStatus().Set(app::StartupPhase::kLoadingConfig);
  dida::g_config_store.Begin();
  dida::g_device_config = dida::g_config_store.Load();

  // 开发便利：编入 device_config_local.h 时**逐字段补齐空缺**，从而跳过
  // 强制门户配网。只填空缺、不覆盖已有值 —— 所以配过一次网之后它不再插手。
  // 想验证配网流程时，把 CONFIG_DIDA_DEV_MODE 关掉或删掉那个头文件。
  if (dev_config::ApplyDefaults(dida::g_device_config)) {
    // 位置可能刚被填入，之前解析出来的 location_id / 经纬度作废
    dida::g_device_config.location_id.clear();
    dida::g_device_config.location_lat.clear();
    dida::g_device_config.location_lon.clear();
    const esp_err_t save_err = dida::g_config_store.Save(dida::g_device_config);
    if (save_err != ESP_OK) {
      ESP_LOGW(kTag, "开发默认配置写回 NVS 失败: %s",
               esp_err_to_name(save_err));
    } else {
      ESP_LOGW(kTag, "开发默认配置已写入 NVS，本次将跳过配网");
    }
  }

  // ---- 背光、光敏、设置初值 ----
  if (!dida::g_backlight.Begin(board::kBacklightPin)) {
    ESP_LOGE(kTag, "背光初始化失败");
  }
  if (!dida::g_light_sensor.Begin(board::kLightSensorAdcUnit,
                                  board::kLightSensorAdcChannel)) {
    ESP_LOGW(kTag, "光敏初始化失败，AUTO 档将退回固定值");
  }

  // 从 NVS 恢复设置（档位与主题），并设定全局主题状态
  dida::g_brightness_mode.store(dida::g_device_config.backlight_mode);
  dida::g_theme_mode.store(dida::g_device_config.theme_mode);
  pages::SetCurrentTheme(
      static_cast<app::ThemeMode>(dida::g_theme_mode.load()));

  // 启动时先读一次环境光并应用，避免等启动任务起来才点亮屏幕
  {
    uint16_t raw = 0;
    if (dida::g_light_sensor.ReadRaw(raw)) {
      dida::g_ambient_brightness.store(backlight::RawToBrightness(raw));
    }
    dida::ApplyBacklight();
  }
  ESP_LOGI(kTag, "设置已恢复: 亮度 %s / 主题 %s",
           backlight::BrightnessModeName(static_cast<backlight::BrightnessMode>(
               dida::g_brightness_mode.load())),
           app::ThemeModeName(
               static_cast<app::ThemeMode>(dida::g_theme_mode.load())));

  ESP_ERROR_CHECK(dida::g_display.Begin());

  // 页面对象与页面管理器（启动期构造，之后不再变动）
  dida::g_pages = std::make_unique<dida::Pages>();
  dida::g_page_manager = std::make_unique<pages::PageManager>();
  dida::RegisterPages();

  if (!dida::g_button.Begin(board::kButtonPin)) {
    ESP_LOGE(kTag, "按键初始化失败，导航不可用");
  }

  // 改 UI 必须持 LVGL 锁（esp_lvgl_port 的 LVGL 任务已在运行）
  if (dida::g_display.Lock(2000)) {
    if (!dida::g_page_manager->CreateAll(app::PageType::kInit)) {
      ESP_LOGE(kTag, "初始页未注册，页面系统未就绪");
    }
    dida::StartButtonPolling();
    dida::g_display.Unlock();
    ESP_LOGI(kTag, "页面系统就绪：已注册 %u 页",
             static_cast<unsigned>(dida::g_page_manager->RegisteredCount()));
  } else {
    ESP_LOGE(kTag, "获取 LVGL 锁超时，页面系统未创建");
  }

  // 启动任务：全部阻塞 I/O 在这一侧。切页通过注入的回调请求，
  // 因此 startup_task 里不会出现任何 UI 调用。
  dida::StartStartupTask(&dida::SwitchToPage);

  ESP_LOGI(kTag, "启动完成 | 空闲内部堆 %u B | 已配置 Wi-Fi: %s",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           config::IsWifiConfigured(dida::g_device_config) ? "是" : "否");
}
