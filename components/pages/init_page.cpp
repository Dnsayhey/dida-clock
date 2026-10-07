#include "pages/init_page.h"

#include <string>

#include "assets/fonts.h"
#include "pages/page_layout.h"
#include "pages/ui_theme.h"

namespace pages {
namespace {

constexpr int kBrandY = 54;
constexpr int kBrandSubY = 92;
constexpr int kBrandDividerY = 126;
constexpr int kDetailY = 268;

}  // namespace

InitPage::InitPage() : Page("init") {}

void InitPage::Create() {
  lv_obj_t* root = CreateFullScreenRoot();

  // 品牌标识用拉丁字：28px 标题字体含 ASCII，够用且不必再生成一套字库。
  brand_label_ = lv_label_create(root);
  lv_obj_set_style_text_font(brand_label_, assets::kFontTitle, 0);
  lv_obj_set_width(brand_label_, kScreenWidth);
  lv_obj_set_pos(brand_label_, 0, kBrandY);
  lv_obj_set_style_text_align(brand_label_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(brand_label_, "DIDA");

  brand_sub_label_ = CreateCenteredLabel(root, kBrandSubY, kScreenWidth);
  lv_label_set_text(brand_sub_label_, "CLOCK");

  divider_ = lv_obj_create(root);
  lv_obj_remove_style_all(divider_);
  lv_obj_set_size(divider_, 100, 1);
  lv_obj_set_pos(divider_, (kScreenWidth - 100) / 2, kBrandDividerY);
  lv_obj_set_style_bg_opa(divider_, LV_OPA_COVER, 0);

  // 进度清单
  for (int i = 0; i < kChecklistRows; ++i) {
    checklist_[i] = CreateCenteredLabel(
        root, kChecklistTop + i * kChecklistStep, kScreenWidth);
  }

  detail_label_ = CreateCenteredLabel(root, kDetailY, kScreenWidth);

  // 故意不放按键提示：本页不响应按键（见类注释）
  ApplyColors();
  Refresh();
}

void InitPage::ApplyColors() {
  const UiColors colors = ColorsForTheme(CurrentTheme());
  lv_obj_set_style_text_color(brand_label_, lv_color_hex(colors.text), 0);
  ApplyTextRole(brand_sub_label_, TextRole::kDim);
  ApplyTextRole(detail_label_, TextRole::kDim);
  lv_obj_set_style_bg_color(divider_, lv_color_hex(colors.text), 0);
  // 清单每行的颜色取决于进度，由 Refresh() 逐行设置。
}

void InitPage::UpdateChecklist(app::StartupPhase current) {
  const UiColors colors = ColorsForTheme(CurrentTheme());
  for (int i = 0; i < kChecklistRows; ++i) {
    const app::StartupPhase step = kChecklist[i];
    uint32_t color = colors.text_dim;  // 未开始
    if (step < current) {
      color = colors.text;  // 已完成
    } else if (step == current) {
      color = colors.accent;  // 进行中
    }
    lv_obj_set_style_text_color(checklist_[i], lv_color_hex(color), 0);
    // LVGL 不做字重，用前缀标记标出"当前这一步"。用 ASCII 的 ">" 而不是
    // ▸(U+25B8)：字库的拉丁部分只覆盖 0x20-0x7E，非 ASCII 符号要额外加字形。
    const std::string text = (step == current ? "> " : "  ") +
                             std::string(app::StartupPhaseName(step));
    lv_label_set_text(checklist_[i], text.c_str());
  }
}

void InitPage::OnThemeChanged(app::ThemeMode mode) {
  ApplyThemeToContainer(Root(), mode);
  ApplyColors();
}

void InitPage::OnEnter() {
  if (timer_ == nullptr) {
    timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
          static_cast<InitPage*>(lv_timer_get_user_data(timer))->Refresh();
        },
        kRefreshIntervalMs, this);
  }
  Refresh();
}

void InitPage::OnLeave() {
  if (timer_ != nullptr) {
    lv_timer_delete(timer_);
    timer_ = nullptr;
  }
}

void InitPage::Refresh() {
  if (detail_label_ == nullptr) {
    return;
  }
  // 快照是拷贝，锁外使用（CONVENTIONS §5）
  const app::StartupStatusSnapshot status = app::GetStartupStatus().Snapshot();

  UpdateChecklist(status.phase);

  // 细节行：Wi-Fi 名、失败原因等。配网等待时给出更明确的指引。
  std::string detail = status.detail;
  if (status.phase == app::StartupPhase::kWaitingForConfig) {
    detail = "请用手机连接热点完成配网";
  }
  lv_label_set_text(detail_label_, detail.c_str());
}

}  // namespace pages
