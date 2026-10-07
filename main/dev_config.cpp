#include "dev_config.h"

#include <esp_log.h>

#include <string>

#include "sdkconfig.h"

// DIDA_HAS_DEVICE_CONFIG_LOCAL 由 main/CMakeLists.txt 传下来（值为 0/1）。
// 不用 __has_include：它的依赖 CMake 看不见，会让"首次创建本地配置"静默失效。
#ifndef DIDA_HAS_DEVICE_CONFIG_LOCAL
#define DIDA_HAS_DEVICE_CONFIG_LOCAL 0
#endif

#if CONFIG_DIDA_DEV_MODE && DIDA_HAS_DEVICE_CONFIG_LOCAL
#include "device_config_local.h"
#define DIDA_HAS_DEV_LOCAL 1
#endif

#ifndef DIDA_HAS_DEV_LOCAL
#define DIDA_HAS_DEV_LOCAL 0
#endif

namespace dev_config {
namespace {

constexpr char kTag[] = "dev_config";

#if DIDA_HAS_DEV_LOCAL
// 填空缺字段。value 为空表示"未提供默认值"，跳过。
// sensitive=true 时**不打印值** —— 密码不能进日志（CONVENTIONS §10）。
bool FillIfMissing(std::string& field, const char* value, const char* name,
                   bool sensitive) {
  if (!field.empty()) {
    ESP_LOGI(kTag, "DEV: %s 已有值，保留", name);
    return false;
  }
  if (value == nullptr || value[0] == '\0') {
    ESP_LOGI(kTag, "DEV: %s 未提供默认值，跳过", name);
    return false;
  }
  field = value;
  if (sensitive) {
    ESP_LOGW(kTag, "DEV: 注入 %s（值不打印）", name);
  } else {
    ESP_LOGW(kTag, "DEV: 注入 %s = \"%s\"", name, value);
  }
  return true;
}
#endif  // DIDA_HAS_DEV_LOCAL

}  // namespace

bool HasDefaults() { return DIDA_HAS_DEV_LOCAL != 0; }

bool ApplyDefaults(config::DeviceConfig& config) {
#if DIDA_HAS_DEV_LOCAL
  ESP_LOGW(kTag, "DEV_MODE 已启用，补齐空缺的开发默认配置");
  bool changed = false;
  changed |= FillIfMissing(config.wifi_ssid, DEV_WIFI_SSID, "WiFi SSID", false);
  changed |=
      FillIfMissing(config.wifi_password, DEV_WIFI_PASSWORD, "WiFi 密码", true);
  changed |= FillIfMissing(config.adm, DEV_ADM, "地区", false);
  changed |= FillIfMissing(config.location, DEV_LOCATION, "位置", false);
  return changed;
#else
  (void)config;
  return false;
#endif
}

}  // namespace dev_config
