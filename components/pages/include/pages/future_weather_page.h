#pragma once

#include <functional>
#include <vector>

#include "app/weather_view_data.h"
#include "pages/page.h"

namespace pages {

// 未来天气页：从**明天**开始的逐日预报，不含当天（当天在实时天气页）。
//
// 版面：
//
//           7日预报              28px 标题
//         ──────────
//         10-08 周四  小雨  22/28℃
//         10-09 周五  多云  21/27℃
//         ...                  最多 6 行
//
// 每行四列各成一个 label：日期（定宽右对齐）、星期（定 x）、天气（按天气
// 上色，与实时天气页同一套配色）、温度（右对齐）。
//
// 日期与星期必须分列：合成 "10-10 周六" 时比例字体下 "1" 比 "0" 窄，
// 10-10 与 10-11 宽度不同，星期就会左右飘。
//
// 分层：本页不认识 store，已格式化的行数据由装配层注入。
class FutureWeatherPage : public Page {
 public:
  using DataSource = std::function<app::ForecastViewData()>;

  explicit FutureWeatherPage(DataSource source);

  void Create() override;
  void OnEnter() override;
  void OnLeave() override;
  void Refresh() override;
  void OnThemeChanged(app::ThemeMode mode) override;

 private:
  void ApplyColors();

  DataSource source_;

  lv_obj_t* title_label_ = nullptr;
  lv_obj_t* divider_ = nullptr;
  std::vector<lv_obj_t*> date_labels_;  // 第 1 列：日期（定宽右对齐）
  std::vector<lv_obj_t*>
      weekday_labels_;  // 第 2 列：星期（定 x，不受日期宽度影响）
  std::vector<lv_obj_t*> weather_labels_;  // 第 3 列：天气（按天气上色）
  std::vector<lv_obj_t*> temp_labels_;     // 第 4 列：温度（右对齐）
  lv_obj_t* status_label_ = nullptr;
  lv_timer_t* timer_ = nullptr;

  // 预报数据 10 分钟才更新一次，5 秒轮到足够，且开销可忽略。
  static constexpr uint32_t kRefreshIntervalMs = 5000;
  static constexpr int kMaxRows = 7;
};

}  // namespace pages
