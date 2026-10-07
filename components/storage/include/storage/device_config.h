#pragma once

// 设备配置的数据模型与纯逻辑判定。
//
// 这个文件**不依赖 ESP-IDF**，可在主机上单测。
// NVS 的实际读写见 nvs_config_store.h。
//
// 命名空间 "dida" 与键名 ssid/pwd/adm/loc/loc_id/loc_lat/loc_lon/blm/theme
// 是**既有设备上已经落盘的格式**，改动会让 OTA 升级后的设备读到空配置、
// 用户被迫重新配网。因此这些字面量一个都不能改。

#include <string>

namespace config {

// NVS 命名空间与键名（既有设备上已落盘的格式，不得改动）
constexpr char kNvsNamespace[] = "dida";
constexpr char kKeyWifiSsid[] = "ssid";
constexpr char kKeyWifiPassword[] = "pwd";
constexpr char kKeyAdm[] = "adm";
constexpr char kKeyLocation[] = "loc";
constexpr char kKeyLocationId[] = "loc_id";
constexpr char kKeyLocationLat[] = "loc_lat";
constexpr char kKeyLocationLon[] = "loc_lon";
constexpr char kKeyBacklightMode[] = "blm";
constexpr char kKeyThemeMode[] = "theme";

struct DeviceConfig {
  std::string wifi_ssid;
  std::string wifi_password;
  std::string adm;          // 地区（用于城市查询的 adm 参数）
  std::string location;     // 位置名（页面展示用）
  std::string location_id;  // 和风天气 location id
  std::string location_lat;
  std::string location_lon;
  int backlight_mode = 0;
  // 编码与 app::ThemeMode 一致（0=深色 1=浅色），默认浅色。
  // NVS 里没有这个键时用的就是这里的值 —— 改它等于改"新设备开机显示什么"。
  int theme_mode = 1;
};

// 去掉首尾空白。用户在网页表单里很容易带进空格。
std::string Trim(const std::string& value);

// 对所有字符串字段做 Trim，并把越界的档位夹到合法区间。
DeviceConfig Normalize(const DeviceConfig& config);

// Wi-Fi 是否已配置：SSID 与密码都必须非空。
// 空密码的开放网络同样算"未配置"——设备不会去连它。
bool IsWifiConfigured(const DeviceConfig& config);

// 位置名是否已配置（页面展示用）
bool IsLocationConfigured(const DeviceConfig& config);

// 是否已具备发起天气查询的条件。
// 三个查询各需要不同的东西：
//   * 城市查询只需要 location/adm
//   * 实时天气与 7 日预报需要 location_id
//   * 空气质量需要经纬度
// 所以"能查天气"= 有 location_id 且有经纬度。
bool IsWeatherQueryReady(const DeviceConfig& config);

}  // namespace config
