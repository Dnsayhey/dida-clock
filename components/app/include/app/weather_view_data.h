#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "store/weather_store.h"
#include "weather/weather_types.h"

namespace app {

// 由 ISO 日期求星期，返回中文（"周一".."周日"）。失败（格式不对）返回空串。
//
// 纯历法换算，主机可测。放在这里而不是页面里，是因为它同时被实时天气页
// 与未来天气页使用，而且要能被单测逐日验证。
std::string WeekdayFromIsoDate(const std::string& iso_date);

// ISO 日期 "YYYY-MM-DD" -> "MM-DD"。格式不对时原样返回。
// 给未来天气页用（那一页要的是紧凑格式）。
std::string ShortDate(const std::string& iso_date);

// ISO 日期 "YYYY-MM-DD" -> "10月07日"。格式不对时返回空串。
std::string ChineseDate(const std::string& iso_date);

// 空气质量短标：官方 category 是"轻度污染"这类长词，徽章只有 38px 宽放不下，
// 统一压成"优 / 良 / 轻度 / 中度 / 重度 / 严重"。
//
// 放在 app 层而不是页面里，是因为它是**纯文本处理**、不碰 LVGL，
// 这样能被主机单测覆盖 —— 而"多字节字符匹配写错"正是这类代码最容易出的问题
// （"晴"和"沙"的 UTF-8 首字节相同，只比首字节会混淆）。
std::string AqiShortLabel(const std::string& category);

// ---------------- 实时天气页：时钟 ----------------
//
// 时钟与天气内容**分开注入**：两者的变化率差三个数量级（秒每秒变，
// 天气最快 30 分钟变一次）。若读时钟必须经过天气数据源，那 1Hz 的刷新
// 就得每秒拷贝一份完整的 WeatherSnapshot —— 代价与收益完全不成比例。
//
// 拆开之后，时钟这一路可以高频调用，天气那一路则只在内容真的变化时重建。
struct ClockViewData {
  std::string date;     // "10月07日"
  std::string weekday;  // "周二"
  std::string hhmm;     // "14:26"
  std::string seconds;  // "37"

  // 当前时刻在秒内的毫秒位置 [0,999]。
  //
  // 页面据此把下一次刷新**对齐到墙上时钟的秒边界**：周期若固定 1000ms，
  // 相位就由定时器创建时刻决定，秒位会在一个随机的时刻跳字 ——
  // 与手机对表时一眼可见。仅在 hhmm 非空（时间已同步）时有意义。
  uint32_t ms_into_second = 0;
};

// 纯逻辑：不读时钟，三个入参都由调用方给出。
//
//   date_text 形如 "2026-10-07"
//   time_text 形如 "14:26:37"
//
// 两者为空表示时间尚未同步，页面会据此不显示日期与时间。
ClockViewData FormatClockView(const std::string& date_text,
                              const std::string& time_text,
                              uint32_t ms_into_second);

// 时钟刷新周期（毫秒）：让下一次触发瞄准**秒边界之后**，并取 lv_tick 能表示的
// 格点中最靠近边界的那一个。
//
// 不能直接用固定 1000ms：lv_timer 的到期判定是 last_run + period，而 last_run
// 在执行回调前被设为"当时"，所以相位由**创建定时器那一刻**决定 —— 秒位会在随机
// 的时刻跳字（与手机对表一眼可见），而且迟到只累积不补偿（跨过整秒就跳秒）。
// 每次按秒内位置重算，相位就固定下来且不累积，SNTP 校时跳变后也自动收敛。
//
// 注意这是**算出来的**周期，不是最终落点：lv_tick 以 tick_grid_ms 步进，真实触发
// 时刻还会再被量化一次（边界前后一个网格之内）。若早于边界，那一轮取到的秒值
// 没变、不产生任何写入，下一轮立即补上 —— 不会累积成可见偏差。
// 契约：ms_into_second ∈ [0,999]；tick_grid_ms 为 0 时退回 1000。
uint32_t ClockRefreshPeriodMs(uint32_t ms_into_second, uint32_t tick_grid_ms);

// ---------------- 实时天气页：天气内容 ----------------
//
// 每个字段对应版面上一个具体槽位，不做通用化：页面要显示什么就只给什么，避免
// 页面自己拼字符串（那会让格式化逻辑散落两处）。不放 location_id、同步状态明细
// 等排查用信息；也**不含日期与时间** —— 它们属于 ClockViewData，混在一起会让
// "只为看秒"也不得不重建整页天气内容。
struct WeatherPageViewData {
  // 滚动信息条：风向 / 体感 / 能见度 / 湿度。
  // 用 " · " 分隔 —— 中点比逗号更像"信息流"，扫读时也更省力。
  std::string ticker;

  std::string city;              // "杭州"（adm2，地级市）
  std::string weather_category;  // "雨"（9 类之一；官方表外的代码为空串）
  std::string weather_text;      // "中到大雨"（官方 text，完整，不截断）
  std::string temperature;       // "22℃"
  std::string aqi_category;      // 官方原文 "优"/"轻度污染"，页面据此取徽章底色
  std::string aqi_short;  // 徽章上显示的短标 "优"/"轻度"，见 AqiShortLabel()

  // 数据尚未就绪时的提示（如"天气同步中"）。就绪时为空。
  std::string status;
};

// 纯逻辑：不读时钟、不碰 LVGL。city_name 是城市名的最后一级回退。
WeatherPageViewData FormatWeatherPage(const store::WeatherSnapshot& snapshot,
                                      const std::string& city_name);

// ---------------- 未来天气页 ----------------

struct ForecastRow {
  std::string date;     // "10-01"
  std::string weekday;  // "周四"
  std::string text;     // "小雨"（官方完整名，不截断）
  // 天气分类（weather::WeatherCategoryName 的结果，可能为空）。
  // 页面据此给"小雨"上色 —— 用 iconDay 而不是 textDay 取分类，
  // 因为 icon 与语言无关，改 lang 不会让配色失效。
  std::string category;
  std::string temp_range;  // "23/29℃"
};

struct ForecastViewData {
  // **从今天开始**的行，与标题「7日预报」一致。
  //
  // 当天那一行的星期显示为"今天"而不是星期几 —— 用户不必自己去数哪天是今天。
  // 它的温度区间（最低/最高）是实时天气页**没有**的信息：那边显示的是当前温度。
  std::vector<ForecastRow> rows;
  std::string status;  // 数据尚未就绪时的提示
};

// today_iso 是本地当天日期（"YYYY-MM-DD"，来自 net::ReadLocalClock().date）。
//
// **不能假定第 0 条就是今天**：7 日预报每 6 小时才同步一次，所以每天
// 00:00~06:00 这段，列表第一条其实还是昨天的日期。传空串（时间未同步）时
// 不做任何日期判断，原样展示。
ForecastViewData FormatForecast(const store::WeatherSnapshot& snapshot,
                                const std::string& today_iso);

}  // namespace app
