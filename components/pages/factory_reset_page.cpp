#include "pages/factory_reset_page.h"

#include <utility>

#include "assets/fonts.h"
#include "pages/page_layout.h"
#include "pages/ui_theme.h"

namespace pages {
namespace {

constexpr int kWarnTop = 128;
constexpr int kWarnStep = 26;
constexpr int kInnerDividerY = 196;
constexpr int kActionY = 218;
constexpr int kBackY = 254;

}  // namespace

FactoryResetPage::FactoryResetPage(ResetAction on_reset)
    : Page("factory_reset"), on_reset_(std::move(on_reset)) {}

void FactoryResetPage::Create() {
  lv_obj_t* root = CreateFullScreenRoot();

  // 标题**不用**危险色：这一页本身不是危险动作，危险的是"长按执行"。
  // 整页染红会让真正需要警示的那一行失去突出。
  title_label_ = CreatePageTitle(root, "恢复出厂");
  divider_ = CreateDivider(root);

  warn_label_ = CreateCenteredLabel(root, kWarnTop, kScreenWidth);
  lv_label_set_text(warn_label_, "将清除所有配置");
  warn2_label_ = CreateCenteredLabel(root, kWarnTop + kWarnStep, kScreenWidth);
  lv_label_set_text(warn2_label_, "Wi-Fi 与位置都会丢失");

  inner_divider_ = lv_obj_create(root);
  lv_obj_remove_style_all(inner_divider_);
  lv_obj_set_size(inner_divider_, kScreenWidth - 2 * kMarginX, 1);
  lv_obj_set_pos(inner_divider_, kMarginX, kInnerDividerY);
  lv_obj_set_style_bg_opa(inner_divider_, LV_OPA_COVER, 0);

  // 只有这一行用危险色 —— 它是唯一会真的清数据的动作。
  action_label_ = CreateCenteredLabel(root, kActionY, kScreenWidth);
  lv_label_set_text(action_label_, "长按执行");

  back_label_ = CreateCenteredLabel(root, kBackY, kScreenWidth);
  lv_label_set_text(back_label_, "双击返回");

  ApplyColors();
}

void FactoryResetPage::ApplyColors() {
  const UiColors colors = ColorsForTheme(CurrentTheme());
  ApplySkeletonColors(title_label_, divider_);
  lv_obj_set_style_bg_color(inner_divider_, lv_color_hex(colors.text), 0);
  ApplyTextRole(warn_label_, TextRole::kText);
  ApplyTextRole(warn2_label_, TextRole::kDim);
  ApplyTextRole(action_label_, TextRole::kDanger);
  ApplyTextRole(back_label_, TextRole::kDim);
}

void FactoryResetPage::OnThemeChanged(app::ThemeMode mode) {
  ApplyThemeToContainer(Root(), mode);
  ApplyColors();
}

void FactoryResetPage::OnLongPress() {
  if (on_reset_) {
    on_reset_();
  }
}

}  // namespace pages
