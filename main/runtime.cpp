#include "runtime.h"

#include <esp_check.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "backlight/backlight_policy.h"
#include "nvs_flash.h"

namespace dida {
namespace {

constexpr char kTag[] = "dida";

}  // namespace

// ---------------- 文件作用域：跨任务存活的对象 ----------------
//
// 全部定义在此，声明见 runtime.h。放文件作用域而不是 app_main 的局部变量，
// 是因为 app_main 返回后主任务栈会被释放（CONVENTIONS §6.2）。

store::WeatherStore g_weather_store;
config::DeviceConfig g_device_config;
display::DisplayDevice g_display;
std::unique_ptr<Pages> g_pages;
std::unique_ptr<pages::PageManager> g_page_manager;
app::AppController g_app_controller;
app::AppCommandExecutor g_command_executor;
input::ButtonInput g_button;

backlight::BacklightDevice g_backlight;
backlight::LightSensor g_light_sensor;
storage::NvsConfigStore g_config_store;
portal::CaptivePortal g_portal;

// 这两个原子量的初值只是"配置读出来之前"的占位；app_main 随后会按
// g_device_config 覆盖。取值与 config::DeviceConfig 的字段默认值保持一致。
std::atomic<int> g_brightness_mode{
    static_cast<int>(backlight::BrightnessMode::kAuto)};
std::atomic<int> g_theme_mode{static_cast<int>(app::ThemeMode::kLight)};
std::atomic<uint8_t> g_ambient_brightness{128};

// ---------------- 共用工具 ----------------

uint64_t UptimeMs() {
  return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

esp_err_t InitNvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(kTag, "NVS 需要重新初始化（%s），执行 erase",
             esp_err_to_name(err));
    ESP_RETURN_ON_ERROR(nvs_flash_erase(), kTag, "nvs_flash_erase failed");
    err = nvs_flash_init();
  }
  return err;
}

void ApplyBacklight() {
  const auto mode =
      static_cast<backlight::BrightnessMode>(g_brightness_mode.load());
  g_backlight.SetDuty(
      backlight::ResolveDuty(mode, g_ambient_brightness.load()));
}

}  // namespace dida
