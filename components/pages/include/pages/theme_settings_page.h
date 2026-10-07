#pragma once

#include <functional>

#include "app/settings_view_data.h"
#include "pages/page.h"

namespace pages {

// 亮度与主题页。
//
// 版面：
//
//            设置               28px 标题
//         ──────────
//         亮度          高       标签左 / 值右，值用强调色
//         主题          深色
//         ──────────
//         单击切换亮度            16px 灰，居中
//         长按切换主题
//         双击进入恢复出厂
//
// 按键语义：单击在 AUTO -> 低 -> 中 -> 高 之间循环；双击由 AppController 处理
// （本页不参与）。分层：页面不认识 backlight / storage —— 动作走注入的回调，
// 显示数据走注入的数据源。
class ThemeSettingsPage : public Page {
 public:
  using DataSource = std::function<app::SettingsViewData()>;

  ThemeSettingsPage(DataSource source,
                    std::function<void()> on_cycle_brightness,
                    std::function<void()> on_toggle_theme);

  void Create() override;
  void OnEnter() override;
  void Refresh() override;
  void OnClick() override;
  void OnLongPress() override;
  void OnThemeChanged(app::ThemeMode mode) override;

 private:
  void ApplyColors();

  DataSource source_;
  std::function<void()> on_cycle_brightness_;
  std::function<void()> on_toggle_theme_;

  lv_obj_t* title_label_ = nullptr;
  lv_obj_t* divider_ = nullptr;         // 标题下的分隔线
  lv_obj_t* inner_divider_ = nullptr;   // 设置值与操作提示之间的分隔线
  lv_obj_t* caption1_label_ = nullptr;  // "亮度"
  lv_obj_t* caption2_label_ = nullptr;  // "主题"
  lv_obj_t* brightness_label_ = nullptr;
  lv_obj_t* theme_label_ = nullptr;
  lv_obj_t* hint_label_ = nullptr;
  lv_obj_t* hint2_label_ = nullptr;
  lv_obj_t* hint3_label_ = nullptr;
};

}  // namespace pages
