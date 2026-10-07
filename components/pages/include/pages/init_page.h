#pragma once

#include <array>

#include "app/runtime_status.h"
#include "pages/page.h"

namespace pages {

// 启动页：显示**真实的启动进度**，而不是一句静态的"正在启动"。
//
// 版面：
//
//            DIDA              28px，拉丁
//            CLOCK             16px 灰
//         ──────────
//          读取配置            完成 = 正文色
//          连接网络            进行中 = 强调色
//          同步时间            未开始 = 灰
//          同步天气
//
// 为什么做成"清单"而不是只显示当前阶段：启动有 4 个可感知的阶段，只显示
// 当前一步的话用户看不出"还剩多少"。清单让进度可度量，卡住时也一眼能看出
// 卡在哪一步。
//
// **本页是启动闸门：不响应任何按键**（见 AppController）。
// 它只能被启动流程自动切走：启动成功 -> 实时天气页，需要配网 -> 配网页。
// 因此本页不显示任何按键提示 —— 显示了也按不动，反而误导。
class InitPage : public Page {
 public:
  InitPage();

  void Create() override;
  void OnEnter() override;
  void OnLeave() override;
  void Refresh() override;
  void OnThemeChanged(app::ThemeMode mode) override;

 private:
  void ApplyColors();
  void UpdateChecklist(app::StartupPhase current);

  // 进度清单上的四个阶段。顺序即进度顺序 —— 用枚举值的大小判断
  // "已完成 / 进行中 / 未开始"，所以这里的顺序必须与 StartupPhase 一致。
  static constexpr int kChecklistRows = 4;
  static constexpr std::array<app::StartupPhase, kChecklistRows> kChecklist = {
      app::StartupPhase::kLoadingConfig,
      app::StartupPhase::kConnectingWifi,
      app::StartupPhase::kSyncingTime,
      app::StartupPhase::kSyncingWeather,
  };
  static constexpr int kChecklistTop = 150;
  static constexpr int kChecklistStep = 26;

  lv_obj_t* brand_label_ = nullptr;
  lv_obj_t* brand_sub_label_ = nullptr;
  lv_obj_t* divider_ = nullptr;
  lv_obj_t* checklist_[kChecklistRows] = {};
  lv_obj_t* detail_label_ = nullptr;
  lv_timer_t* timer_ = nullptr;

  // 启动阶段刷新周期。进度变化不频繁，500ms 足够且开销小。
  static constexpr uint32_t kRefreshIntervalMs = 500;
};

}  // namespace pages
