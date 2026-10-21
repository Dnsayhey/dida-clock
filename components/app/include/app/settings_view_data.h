#pragma once

#include <string>

#include "backlight/backlight_policy.h"

namespace app {

// 主题档位。数值与 NVS 里存的 theme 值一致，**新增档位只能追加到末尾**。
// 数值顺序就是按键循环顺序（浅色 → 深色 → 自动），因此 NextThemeMode 只需
// 轮转一位。默认"浅色"，见 config::DeviceConfig。
//
// "自动"跟**日出日落**走（判定见 auto_theme.h）；亮度的"自动"跟**光敏**走。
// 两个"自动"各管一件事，互不影响。
enum class ThemeMode {
  kLight = 0,
  kDark = 1,
  kAuto = 2,
  kCount = 3,
};

const char* ThemeModeName(ThemeMode mode);

struct SettingsViewData {
  std::string brightness;  // "自动" / "低" / "中" / "高"
  std::string theme;       // "浅色" / "深色" / "自动"
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
