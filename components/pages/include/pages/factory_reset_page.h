#pragma once

#include <functional>

#include "pages/page.h"

namespace pages {

// 恢复出厂页。
//
// 版面：
//
//           恢复出厂             28px 标题（不用危险色，见实现）
//         ──────────
//         将清除所有配置
//         Wi-Fi 与位置都会丢失      16px 灰
//         ──────────
//         长按执行                 危险红 —— 全页只有这一行是红的
//         双击返回                 16px 灰
//
// 按键语义：
//   长按 -> 清 NVS 并重启（高风险操作，只由长按触发）
//   双击 -> 由 AppController 处理，返回实时天气页（本页不参与）
//   单击 -> 无动作
//
// 分层：页面不认识 NVS，重置动作通过注入的回调执行 ——
// 这样"清 NVS + 重启"这种底层副作用不会被页面层偷偷做掉。
class FactoryResetPage : public Page {
 public:
  using ResetAction = std::function<void()>;

  explicit FactoryResetPage(ResetAction on_reset);

  void Create() override;
  void OnLongPress() override;
  void OnThemeChanged(app::ThemeMode mode) override;

 private:
  void ApplyColors();

  ResetAction on_reset_;
  lv_obj_t* title_label_ = nullptr;
  lv_obj_t* divider_ = nullptr;
  lv_obj_t* warn_label_ = nullptr;
  lv_obj_t* warn2_label_ = nullptr;
  lv_obj_t* inner_divider_ = nullptr;
  lv_obj_t* action_label_ = nullptr;
  lv_obj_t* back_label_ = nullptr;
};

}  // namespace pages
