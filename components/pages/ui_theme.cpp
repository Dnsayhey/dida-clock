#include "pages/ui_theme.h"

#include <lvgl.h>

#include <array>
#include <string>

namespace pages {
namespace {

// 深色：黑底白字
constexpr UiColors kDarkColors = {
    .background = 0x000000,
    .text = 0xFFFFFF,
    .text_dim = 0x888888,
    .accent = 0xFF8D00,
};

// 浅色：白底黑字
constexpr UiColors kLightColors = {
    .background = 0xFFFFFF,
    .text = 0x000000,
    .text_dim = 0x666666,
    // 浅色主题下不能沿用 #FF8D00：它在白底上的对比度只有 2.3。
    // 这是同一色调的暗版，对比度 4.6。
    .accent = 0xB85C00,
};

// 危险色两种主题下都保持红，确保"恢复出厂"这类提示不会因主题而失去警示性
constexpr uint32_t kDangerColor = 0xFF4040;

// 徽章上的文字色。徽章底色都是暗色（见 kAqiColors），白字与它们的最低
// 对比度是 4.8，两种主题下都成立，所以不需要按主题分两版。
constexpr uint32_t kBadgeTextColor = 0xFFFFFF;

// 天气文字色。深色主题用亮版、浅色主题用暗版，两版都按对比度 ≥4.5 挑选。
//
// 匹配用**完整前缀**而不是首字节：UTF-8 里"晴"和"沙"的首字节都是 0xE6，
// 只看首字节会把它们混为一谈。
struct WeatherColorEntry {
  const char* name;
  uint32_t dark;
  uint32_t light;
};

// 9 类与 weather::WeatherCategoryName() 一一对应。
constexpr std::array<WeatherColorEntry, 9> kWeatherColors = {{
    {"晴", 0xFF8D00, 0xB85C00},  // 暖橙
    {"多云", 0xA8B8CC, 0x4A5A70},
    {"阴", 0xA8B8CC, 0x4A5A70},
    {"雨", 0x4A9EFF, 0x1D5FB8},
    {"雷", 0xFFB020, 0x8A5A00},
    {"冰雹", 0x22D3EE, 0x0E7490},
    {"雪", 0x22D3EE, 0x0E7490},
    {"雾", 0xB0B0B0, 0x5A5A5A},
    {"沙尘", 0xD2A679, 0x7A5A32},
}};

// 空气质量徽章底色。白字压在上面，所以全部取暗色；
// 与白字的对比度依次为 5.4 / 4.9 / 4.8 / 7.0 / 10.2 / 14.6。
// "优"用绿、"良"用土黄、污染用橙红到深红，随严重度递进。
struct AqiColorEntry {
  const char* label;  // 官方 category 的前缀
  uint32_t background;
};

constexpr std::array<AqiColorEntry, 6> kAqiColors = {{
    {"优", 0x1E7A34},
    {"良", 0x8A6D00},
    {"轻度", 0xB8560A},
    {"中度", 0xA33114},
    {"重度", 0x7A1F2B},
    {"严重", 0x4A1430},
}};

// s 是否以 prefix 开头。
bool HasPrefix(const std::string& s, const char* prefix) {
  return s.rfind(prefix, 0) == 0;
}

app::ThemeMode g_current_theme = app::ThemeMode::kDark;

}  // namespace

void SetCurrentTheme(app::ThemeMode mode) { g_current_theme = mode; }
app::ThemeMode CurrentTheme() { return g_current_theme; }

UiColors ColorsForTheme(app::ThemeMode mode) {
  return mode == app::ThemeMode::kLight ? kLightColors : kDarkColors;
}

uint32_t ColorForRole(const UiColors& colors, TextRole role) {
  switch (role) {
    case TextRole::kText:
      return colors.text;
    case TextRole::kDim:
      return colors.text_dim;
    case TextRole::kDanger:
      return kDangerColor;
    case TextRole::kOnBadge:
      return kBadgeTextColor;
  }
  return colors.text;
}

void ApplyTextRole(lv_obj_t* label, TextRole role) {
  if (label == nullptr) {
    return;
  }
  const UiColors colors = ColorsForTheme(CurrentTheme());
  lv_obj_set_style_text_color(label, lv_color_hex(ColorForRole(colors, role)),
                              0);
}

void ApplyThemeToContainer(lv_obj_t* container, app::ThemeMode mode) {
  if (container == nullptr) {
    return;
  }
  const UiColors colors = ColorsForTheme(mode);
  lv_obj_set_style_bg_color(container, lv_color_hex(colors.background), 0);
  lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
}

uint32_t WeatherColor(const std::string& category) {
  const bool dark = CurrentTheme() == app::ThemeMode::kDark;
  for (const WeatherColorEntry& e : kWeatherColors) {
    if (category == e.name) {
      return dark ? e.dark : e.light;
    }
  }
  // 空串或不认识的分类一律退回正文色。不猜 —— 猜错会传递错误信息。
  return ColorsForTheme(CurrentTheme()).text;
}

uint32_t AqiBadgeColor(const std::string& category) {
  for (const AqiColorEntry& e : kAqiColors) {
    if (HasPrefix(category, e.label)) {
      return e.background;
    }
  }
  // 认不出时用中性灰，仍能保证白字可读。
  return 0x414041;
}

}  // namespace pages
