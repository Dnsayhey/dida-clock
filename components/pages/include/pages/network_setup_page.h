#pragma once

#include "pages/page.h"

namespace pages {

// 配网页：告诉用户**连哪个热点、密码多少、浏览器开哪个地址**。
//
// 版面：
//
//            配网               28px 标题
//         ──────────            分隔线
//         热点名称               16px 灰
//         DIDA-1465             强调色 —— 最关键的信息
//         密码
//         12345678
//         连接后打开任意网页       16px 灰
//         即可设置 Wi-Fi
//
// 信息来自 app::PortalStatus，由启动任务在门户启动后发布。
//
// 配网表单保存后设备会重启，因此本页不需要显示"正在连接"——
// 那个阶段由重启后的 INIT 页负责。
class NetworkSetupPage : public Page {
 public:
  NetworkSetupPage();

  void Create() override;
  void OnEnter() override;
  void OnLeave() override;
  void Refresh() override;
  void OnThemeChanged(app::ThemeMode mode) override;

 private:
  void ApplyColors();
  // 门户未就绪时把配置项整组藏起来，只留"正在启动热点"这一句。
  void SetProvisionVisible(bool visible);

  lv_obj_t* title_label_ = nullptr;
  lv_obj_t* divider_ = nullptr;
  lv_obj_t* ssid_caption_ = nullptr;
  lv_obj_t* ssid_label_ = nullptr;
  lv_obj_t* password_caption_ = nullptr;
  lv_obj_t* password_label_ = nullptr;
  lv_obj_t* url_caption_ = nullptr;
  lv_obj_t* url_label_ = nullptr;
  lv_obj_t* hint_label_ = nullptr;
  lv_timer_t* timer_ = nullptr;

  static constexpr uint32_t kRefreshIntervalMs = 1000;
};

}  // namespace pages
