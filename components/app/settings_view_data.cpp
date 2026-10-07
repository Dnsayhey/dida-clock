#include "app/settings_view_data.h"

namespace app {

const char* ThemeModeName(ThemeMode mode) {
  switch (mode) {
    case ThemeMode::kDark:
      return "深色";
    case ThemeMode::kLight:
      return "浅色";
  }
  return "未知";
}

SettingsViewData FormatSettingsView(backlight::BrightnessMode brightness_mode,
                                    ThemeMode theme_mode) {
  SettingsViewData data;

  // 这里只给**值**。"亮度/主题"这些标签由设置页自己显示 ——
  // 格式化层若拼出完整句子，页面就没法把标签与值分成左右两栏对齐。
  switch (brightness_mode) {
    case backlight::BrightnessMode::kAuto:
      data.brightness = "自动";
      break;
    case backlight::BrightnessMode::kLevel1:
      data.brightness = "低";
      break;
    case backlight::BrightnessMode::kLevel2:
      data.brightness = "中";
      break;
    case backlight::BrightnessMode::kLevel3:
      data.brightness = "高";
      break;
    case backlight::BrightnessMode::kCount:
      data.brightness = "--";
      break;
  }

  data.theme = ThemeModeName(theme_mode);
  return data;
}

backlight::BrightnessMode NextBrightnessMode(
    backlight::BrightnessMode current) {
  return backlight::NextMode(current);
}

ThemeMode NextThemeMode(ThemeMode current) {
  return current == ThemeMode::kDark ? ThemeMode::kLight : ThemeMode::kDark;
}

}  // namespace app
