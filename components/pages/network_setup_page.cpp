#include "pages/network_setup_page.h"

#include <string>

#include "app/runtime_status.h"
#include "assets/fonts.h"
#include "pages/page_layout.h"
#include "pages/ui_theme.h"

namespace pages {
namespace {

// 三组"标签 + 值"，每组占 56px（标签 18 + 间隔 22 + 值 18，组间再留 16px）。
//
// 行位置必须**由组序号显式推出**，提示行放在最后一组之下留明确间隔 ——
// 各处各自写死 y 时，值行（算到 238）与提示行（固定 236）会重叠。
constexpr int kRowTop = 92;          // 第一组标签的 y
constexpr int kRowGroupStep = 56;    // 组间距
constexpr int kCaptionToValue = 22;  // 标签到值的间隔
constexpr int kHintY = 256;          // 最后一组的值在 226，间隔 30px
constexpr int kTextWidth = kScreenWidth - 2 * kMarginX - 8;

}  // namespace

NetworkSetupPage::NetworkSetupPage() : Page("network_setup") {}

void NetworkSetupPage::Create() {
  lv_obj_t* root = CreateFullScreenRoot();

  title_label_ = CreatePageTitle(root, "配网");
  divider_ = CreateDivider(root);

  const int ssid_y = kRowTop;
  const int password_y = kRowTop + kRowGroupStep;
  const int url_y = kRowTop + 2 * kRowGroupStep;

  ssid_caption_ = CreateBodyLabel(root, kMarginX + 4, ssid_y, kTextWidth);
  lv_label_set_text(ssid_caption_, "热点名称");
  ssid_label_ =
      CreateBodyLabel(root, kMarginX + 4, ssid_y + kCaptionToValue, kTextWidth);

  password_caption_ =
      CreateBodyLabel(root, kMarginX + 4, password_y, kTextWidth);
  lv_label_set_text(password_caption_, "密码");
  password_label_ = CreateBodyLabel(root, kMarginX + 4,
                                    password_y + kCaptionToValue, kTextWidth);

  // URL 也给一个标签，与上面两组结构一致 —— 否则它就是一个孤零零的 IP。
  url_caption_ = CreateBodyLabel(root, kMarginX + 4, url_y, kTextWidth);
  lv_label_set_text(url_caption_, "访问地址");
  url_label_ =
      CreateBodyLabel(root, kMarginX + 4, url_y + kCaptionToValue, kTextWidth);

  hint_label_ = CreateCenteredLabel(root, kHintY, kScreenWidth);
  lv_label_set_text(hint_label_, "连接后打开浏览器\n访问上方地址即可设置");

  ApplyColors();
  Refresh();
}

void NetworkSetupPage::ApplyColors() {
  ApplySkeletonColors(title_label_, divider_);
  // 热点名用**品牌强调色**：它是整页最关键的一条，用户要照着它去连。
  lv_obj_set_style_text_color(
      ssid_label_, lv_color_hex(ColorsForTheme(CurrentTheme()).accent), 0);
  // 与设置页同一处理：字段标签是主体信息（"这行是什么"），不是说明性
  // 文字，所以用正文色而不是灰色。
  ApplyTextRole(ssid_caption_, TextRole::kText);
  ApplyTextRole(password_caption_, TextRole::kText);
  ApplyTextRole(password_label_, TextRole::kText);
  ApplyTextRole(url_caption_, TextRole::kText);
  ApplyTextRole(url_label_, TextRole::kText);
  ApplyTextRole(hint_label_, TextRole::kDim);
}

void NetworkSetupPage::OnThemeChanged(app::ThemeMode mode) {
  ApplyThemeToContainer(Root(), mode);
  ApplyColors();
}

void NetworkSetupPage::OnEnter() {
  if (timer_ == nullptr) {
    timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
          static_cast<NetworkSetupPage*>(lv_timer_get_user_data(timer))
              ->Refresh();
        },
        kRefreshIntervalMs, this);
  }
  Refresh();
}

void NetworkSetupPage::OnLeave() {
  if (timer_ != nullptr) {
    lv_timer_delete(timer_);
    timer_ = nullptr;
  }
}

void NetworkSetupPage::SetProvisionVisible(bool visible) {
  lv_obj_t* rows[] = {ssid_caption_,   ssid_label_,  password_caption_,
                      password_label_, url_caption_, url_label_};
  for (lv_obj_t* obj : rows) {
    if (obj == nullptr) {
      continue;
    }
    if (visible) {
      lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void NetworkSetupPage::Refresh() {
  if (ssid_label_ == nullptr) {
    return;
  }
  const app::PortalStatusSnapshot status = app::GetPortalStatus().Snapshot();

  if (!status.running) {
    // 门户还没起来（或已停止）。**不要把空信息当成功信息展示** ——
    // "热点名称："后面跟一片空白会让人以为设备坏了。
    SetProvisionVisible(false);
    lv_label_set_text(hint_label_, "正在启动热点\n请稍候");
    return;
  }

  SetProvisionVisible(true);
  lv_label_set_text(ssid_label_, status.ssid.c_str());
  lv_label_set_text(password_label_, status.password.c_str());
  lv_label_set_text(url_label_, status.ip.c_str());
  lv_label_set_text(hint_label_, "连接后打开浏览器\n访问上方地址即可设置");
}

}  // namespace pages
