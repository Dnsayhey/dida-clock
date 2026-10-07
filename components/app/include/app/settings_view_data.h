#pragma once

#include <string>

#include "backlight/backlight_policy.h"

namespace app {

// 主题档位。0=深色 1=浅色，与 NVS 里存的 theme 值一致（默认浅色，
// 见 config::DeviceConfig 的 theme_mode 字段）。
enum class ThemeMode {
  kDark = 0,
  kLight = 1,
};

const char* ThemeModeName(ThemeMode mode);

struct SettingsViewData {
  std::string brightness;  // "自动" / "低" / "中" / "高"
  std::string theme;       // "深色" / "浅色"
};

// 纯逻辑：由档位与主题生成"设置"页要显示的值。
// 不碰 NVS、不碰 LEDC、不碰 LVGL —— 可在主机上单测。
SettingsViewData FormatSettingsView(backlight::BrightnessMode brightness_mode,
                                    ThemeMode theme_mode);

// 档位轮换（转发到 backlight 的策略，便于页面只依赖 app）
backlight::BrightnessMode NextBrightnessMode(backlight::BrightnessMode current);

// 主题切换
ThemeMode NextThemeMode(ThemeMode current);

}  // namespace app
