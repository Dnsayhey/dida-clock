#include "storage/device_config.h"

#include <algorithm>
#include <cctype>

namespace config {
namespace {

// 档位上限。两者**不能共用一个数**：背光是 4 档（AUTO / LEVEL_1..3），
// 主题只有 2 档（0=深色 1=浅色，见 app::ThemeMode）—— 共用会让 2/3 这种
// 不存在的主题值存进 NVS（屏幕上按深色显示、设置页显示"未知"）。
constexpr int kMaxBrightnessMode = 3;
constexpr int kMaxThemeMode = 1;

int ClampTo(int value, int max_value) {
  if (value < 0) {
    return 0;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

}  // namespace

std::string Trim(const std::string& value) {
  const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };

  auto begin = std::find_if(value.begin(), value.end(), not_space);
  auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
  if (begin >= end) {
    return {};
  }
  return std::string(begin, end);
}

DeviceConfig Normalize(const DeviceConfig& input) {
  DeviceConfig out = input;
  out.wifi_ssid = Trim(input.wifi_ssid);
  // 密码**不做 Trim** —— 首尾空格可能是密码的一部分，改动它会导致连不上。
  // 但要去掉用户误粘贴的换行/回车。
  out.wifi_password = input.wifi_password;
  out.wifi_password.erase(
      std::remove_if(out.wifi_password.begin(), out.wifi_password.end(),
                     [](char c) { return c == '\n' || c == '\r'; }),
      out.wifi_password.end());

  out.adm = Trim(input.adm);
  out.location = Trim(input.location);
  out.location_id = Trim(input.location_id);
  out.location_lat = Trim(input.location_lat);
  out.location_lon = Trim(input.location_lon);
  out.backlight_mode = ClampTo(input.backlight_mode, kMaxBrightnessMode);
  out.theme_mode = ClampTo(input.theme_mode, kMaxThemeMode);
  return out;
}

bool IsWifiConfigured(const DeviceConfig& config) {
  return !config.wifi_ssid.empty() && !config.wifi_password.empty();
}

bool IsLocationConfigured(const DeviceConfig& config) {
  return !config.location.empty();
}

bool IsWeatherQueryReady(const DeviceConfig& config) {
  return !config.location_id.empty() && !config.location_lat.empty() &&
         !config.location_lon.empty();
}

}  // namespace config
