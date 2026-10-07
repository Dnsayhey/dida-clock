#include "pages/page_layout.h"

#include "assets/fonts.h"
#include "pages/ui_theme.h"

namespace pages {
namespace {

// 分隔线宽度：左右各留 kMarginX
constexpr int kDividerWidth = kScreenWidth - 2 * kMarginX;
constexpr int kDividerX = kMarginX;

}  // namespace

lv_obj_t* CreatePageTitle(lv_obj_t* parent, const char* text) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, assets::kFontTitle, 0);
  lv_obj_set_width(label, kScreenWidth);
  lv_obj_set_pos(label, 0, kTitleY);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
  lv_label_set_text(label, text);
  return label;
}

lv_obj_t* CreateDivider(lv_obj_t* parent) {
  lv_obj_t* divider = lv_obj_create(parent);
  lv_obj_remove_style_all(divider);
  lv_obj_set_size(divider, kDividerWidth, 1);
  lv_obj_set_pos(divider, kDividerX, kDividerY);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
  return divider;
}

lv_obj_t* CreateBodyLabel(lv_obj_t* parent, int x, int y, int width) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, assets::kFontBody, 0);
  lv_obj_set_pos(label, x, y);
  if (width > 0) {
    lv_obj_set_width(label, width);
  }
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
  return label;
}

lv_obj_t* CreateCenteredLabel(lv_obj_t* parent, int y, int width) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, assets::kFontBody, 0);
  lv_obj_set_width(label, width);
  lv_obj_set_pos(label, 0, y);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
  return label;
}

void CreateLabelValueRow(lv_obj_t* parent, int y, const char* label_text,
                         lv_obj_t** out_label, lv_obj_t** out_value) {
  // 标签靠左、值靠右。两者宽度都固定，这样值不会因为文本变长而把标签挤走。
  lv_obj_t* label = CreateBodyLabel(parent, kMarginX + 6, y, 90);
  lv_label_set_text(label, label_text);
  lv_obj_t* value = lv_label_create(parent);
  lv_obj_set_style_text_font(value, assets::kFontBody, 0);
  lv_obj_set_width(value, 110);
  lv_obj_set_pos(value, kScreenWidth - kMarginX - 6 - 110, y);
  lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(value, LV_LABEL_LONG_MODE_DOTS);
  if (out_label != nullptr) {
    *out_label = label;
  }
  if (out_value != nullptr) {
    *out_value = value;
  }
}

void ApplySkeletonColors(lv_obj_t* title, lv_obj_t* divider) {
  ApplyTextRole(title, TextRole::kText);
  if (divider != nullptr) {
    lv_obj_set_style_bg_color(
        divider, lv_color_hex(ColorsForTheme(CurrentTheme()).text), 0);
  }
}

}  // namespace pages
