#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "app/weather_view_data.h"
#include "pages/page.h"

namespace pages {

// 实时天气页：面向**日常使用**的少而精视图。
//
// 版面（240x320）：
//
//         10月07日  周二        16px 灰，居中
//           14:26          37   70px 主题色 + 24px 红（**顶部对齐**）
//      ────────────────────     分隔线
//      东风 4级 · 体感 25℃ ...    16px 灰，循环滚动
//      ────────────────────
//      雨           22℃          44px 天气色 + 26px 强调色
//      中到大雨     杭州 [优]      16px 灰 + 16px 主题色 + 徽章
//                          [太空人]
//
// 版面约束：不放标题（省下的 55px 让时间从 64px 长到 70px）；信息限制在左侧
// 160px，右侧 80px 完整留给太空人；天气用「大字分类 + 小字完整名」，不丢雨量
// 与雾霾的区别。
//
// 刷新分**三条路径，不能合并**（按变化性质划分，不是按内容种类）：
//   * 时钟 1Hz —— 唯一真正周期性的东西；每次按墙上时钟的秒内位置重排周期，
//     使跳字落在秒边界之后。它同时是渲染循环的验收探针：卡顿会立刻可见。
//   * 天气内容 —— 非周期事件（net 任务写入，最快 30 分钟一次），靠版本号判脏，
//     **不配定时器**：周期猜短了白跑、猜长了数据到了却不上屏。
//   * 太空人 —— 动画，35ms 一帧。
//
// 不要退回"一个 1Hz 定时器重建整页"：LVGL 对 set_text 与设置样式都是**无条件**
// invalidate（不比较内容是否变了），重建一次就是十几处失效、覆盖大半屏，而每秒
// 真正变化的只有秒位那一个 label。
//
// 分层：本页不认识 net / store，时钟与已格式化的数据都由装配层注入。
class RealTimeWeatherPage : public Page {
 public:
  using ClockSource = std::function<app::ClockViewData()>;
  // 内容版本号：判脏只要这一次整数读取，不必先拷一份完整快照。
  using ContentVersion = std::function<uint32_t()>;
  using DataSource = std::function<app::WeatherPageViewData()>;

  RealTimeWeatherPage(ClockSource clock, ContentVersion content_version,
                      DataSource content);

  void Create() override;
  void OnEnter() override;
  void OnLeave() override;
  void Refresh() override;
  void OnThemeChanged(app::ThemeMode mode) override;

 private:
  void ApplyColors(const app::WeatherPageViewData& data);
  void AdvanceAstronaut();

  // 时钟定时器的回调：先刷时钟（并重排下一周期），再按版本号决定是否重建内容。
  void OnTick();
  void RefreshClock();
  // version 必须在取数据**之前**读到并传进来，理由见 .cpp。
  void RefreshContent(uint32_t version);
  uint32_t CurrentContentVersion() const;

  ClockSource clock_;
  ContentVersion content_version_;
  DataSource content_;

  lv_obj_t* date_label_ = nullptr;          // 日期 + 星期
  lv_obj_t* time_label_ = nullptr;          // 时:分（冒号用强调色）
  lv_obj_t* seconds_label_ = nullptr;       // 秒
  lv_obj_t* divider_ = nullptr;             // 分隔线
  lv_obj_t* ticker_label_ = nullptr;        // 滚动信息条
  lv_obj_t* weather_label_ = nullptr;       // 天气大字（9 类短名）
  lv_obj_t* weather_text_label_ = nullptr;  // 天气完整名
  lv_obj_t* temperature_label_ = nullptr;   // 温度
  lv_obj_t* city_label_ = nullptr;          // 城市（adm2）
  lv_obj_t* aqi_badge_ = nullptr;           // 空气质量徽章底
  lv_obj_t* aqi_label_ = nullptr;           // 徽章文字
  lv_obj_t* astro_image_ = nullptr;         // 太空人
  lv_obj_t* status_label_ = nullptr;        // 数据未就绪时的提示

  lv_timer_t* refresh_timer_ = nullptr;
  lv_timer_t* astro_timer_ = nullptr;
  unsigned astro_frame_ = 0;

  // 上一次真正写进 label 的文本。LVGL 对**相同**的文本同样要 malloc、
  // 重新测量字形并重绘，所以"没变就不写"必须由本页自己判断。
  //
  // 日期与星期分开存：拼接后的串超过 SSO 阈值，每秒拼一次就是每秒一次
  // 堆分配，而两段各自都装得下（见 .cpp）。
  std::string shown_date_;
  std::string shown_weekday_;
  std::string shown_hhmm_;
  std::string shown_seconds_;
  uint32_t last_content_version_ = 0;

  static constexpr uint32_t kSecondMs = 1000;
  // lv_tick 的推进粒度：esp_lvgl_port 用 5ms 的 esp_timer 周期喂 lv_tick_inc，
  // 因此到期判定只能落在它的整数倍上，重排周期也取整到这一格。
  //
  // 这个 5 由 display 组件的 port_cfg.timer_period_ms 显式设定（display_device.cpp）。
  // 那边改动必须同时改这里：失配不会报错，只会让秒位的相位偏差变大。
  static constexpr uint32_t kTickGridMs = 5;
  // 太空人帧间隔。写 35 而不是 33：受 kTickGridMs 限制，33 实际也只能跑到 35。
  static constexpr uint32_t kAstroIntervalMs = 35;
};

}  // namespace pages
