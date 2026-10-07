#include "pages/theme_settings_page.h"

#include <string>
#include <utility>

#include "assets/fonts.h"
#include "pages/page_layout.h"
#include "pages/ui_theme.h"

namespace pages {
namespace {

// 只有两行设置，行距比更多行时放宽一些，让版面不至于上挤下空。
constexpr int kRowTop = 112;
constexpr int kRowStep = 48;
constexpr int kHintTop = 232;
constexpr int kHintStep = 22;
constexpr int kInnerDividerY = 208;

lv_obj_t* MakeHint(lv_obj_t* parent, int y) {
  lv_obj_t* label = CreateCenteredLabel(parent, y, kScreenWidth);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  return label;
}

}  // namespace

ThemeSettingsPage::ThemeSettingsPage(DataSource source,
                                     std::function<void()> on_cycle_brightness,
                                     std::function<void()> on_toggle_theme)
    : Page("theme_settings"),
      source_(std::move(source)),
      on_cycle_brightness_(std::move(on_cycle_brightness)),
      on_toggle_theme_(std::move(on_toggle_theme)) {}

void ThemeSettingsPage::Create() {
  lv_obj_t* root = CreateFullScreenRoot();

  title_label_ = CreatePageTitle(root, "设置");
  divider_ = CreateDivider(root);

  // 两行"标签 —— 值"：标签左、值右。用固定两栏而不是拼一行文本，
  // 是为了让两个值严格右对齐。
  //
  // 标签对象也要接住：不接的话它们的颜色从没被设置过，会落到 LVGL 的默认
  // 前景色（黑），深色主题下就完全看不见。
  //
  // **只列用户能改的项**：内部传感器读数用户拿它做不了任何事，
  // 显示出来只会让人问"这个数我该拿它做什么"。
  CreateLabelValueRow(root, kRowTop, "亮度", &caption1_label_,
                      &brightness_label_);
  CreateLabelValueRow(root, kRowTop + kRowStep, "主题", &caption2_label_,
                      &theme_label_);

  // 内部分隔线：把"设置值"与"操作提示"分开
  inner_divider_ = lv_obj_create(root);
  lv_obj_remove_style_all(inner_divider_);
  lv_obj_set_size(inner_divider_, kScreenWidth - 2 * kMarginX, 1);
  lv_obj_set_pos(inner_divider_, kMarginX, kInnerDividerY);
  lv_obj_set_style_bg_opa(inner_divider_, LV_OPA_COVER, 0);

  hint_label_ = MakeHint(root, kHintTop);
  lv_label_set_text(hint_label_, "单击切换亮度");
  hint2_label_ = MakeHint(root, kHintTop + kHintStep);
  lv_label_set_text(hint2_label_, "长按切换主题");
  hint3_label_ = MakeHint(root, kHintTop + 2 * kHintStep);
  lv_label_set_text(hint3_label_, "双击进入恢复出厂");

  ApplyColors();
  Refresh();
}

void ThemeSettingsPage::ApplyColors() {
  const UiColors colors = ColorsForTheme(CurrentTheme());
  ApplySkeletonColors(title_label_, divider_);
  lv_obj_set_style_bg_color(inner_divider_, lv_color_hex(colors.text), 0);
  // **标签用正文色，不用灰色。**
  //
  // 设置行的标签在告诉你"这一行是什么"，是主体信息而非补充说明；
  // **层次靠颜色，不靠亮度差** —— 值用强调色就能跳出来，标签不必压暗。
  // 若把标签压成 kDim，它会比它解释的值更暗，屏幕上最弱的一环就变成
  // 你必须先读的那一环。
  for (lv_obj_t* caption : {caption1_label_, caption2_label_}) {
    ApplyTextRole(caption, TextRole::kText);
  }
  for (lv_obj_t* value : {brightness_label_, theme_label_}) {
    lv_obj_set_style_text_color(value, lv_color_hex(colors.accent), 0);
  }
  ApplyTextRole(hint_label_, TextRole::kDim);
  ApplyTextRole(hint2_label_, TextRole::kDim);
  ApplyTextRole(hint3_label_, TextRole::kDim);
}

void ThemeSettingsPage::OnEnter() {
  // 本页展示的两个值（亮度档位、主题）**只在按键时变化**，而两条按键路径
  // 都已调用 PageManager::RefreshCurrent()，所以这里只需要在进入页面时刷一次，
  // **不需要周期性定时器** —— 每秒重建一次视图数据却永远不会变，是纯浪费。
  Refresh();
}

void ThemeSettingsPage::OnThemeChanged(app::ThemeMode mode) {
  ApplyThemeToContainer(Root(), mode);
  ApplyColors();
}

void ThemeSettingsPage::Refresh() {
  if (source_ == nullptr || brightness_label_ == nullptr) {
    return;
  }
  const app::SettingsViewData data = source_();
  lv_label_set_text(brightness_label_, data.brightness.c_str());
  lv_label_set_text(theme_label_, data.theme.c_str());
}

void ThemeSettingsPage::OnClick() {
  if (on_cycle_brightness_) {
    on_cycle_brightness_();
  }
}

void ThemeSettingsPage::OnLongPress() {
  if (on_toggle_theme_) {
    on_toggle_theme_();
  }
}

}  // namespace pages
