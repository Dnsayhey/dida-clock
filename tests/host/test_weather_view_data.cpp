// 天气页视图数据与运行期状态的单测（主机运行）。
//
// 历法换算（星期几）与"数据未就绪时的显示"是两类最容易出错、又最难在
// 实机上察觉的问题：星期算错只有盯着看才发现；未就绪状态处理不好会
// 显示空白或残留上一次的数据。
//
// 基准日期（星期几）都是用系统 `date` 命令核对过的，不是手算的。

#include <string>
#include <thread>
#include <vector>

#include "app/runtime_status.h"
#include "app/weather_view_data.h"
#include "test_framework.h"

using app::AqiShortLabel;
using app::ChineseDate;
using app::ClockRefreshPeriodMs;
using app::FormatClockView;
using app::FormatForecast;
using app::FormatWeatherPage;
using app::PortalStatus;
using app::ShortDate;
using app::StartupPhase;
using app::StartupPhaseName;
using app::StartupStatus;
using app::WeekdayFromIsoDate;
using store::WeatherStore;
using weather::SyncStatus;

// ---------------- 历法换算 ----------------

TEST_CASE(Calendar_星期换算与系统一致) {
  CHECK_EQ(WeekdayFromIsoDate("1970-01-01"), std::string("周四"));
  CHECK_EQ(WeekdayFromIsoDate("2000-01-01"), std::string("周六"));
  CHECK_EQ(WeekdayFromIsoDate("2026-01-01"), std::string("周四"));
  CHECK_EQ(WeekdayFromIsoDate("2026-09-30"), std::string("周三"));
  CHECK_EQ(WeekdayFromIsoDate("2026-10-01"), std::string("周四"));
}

// 闰日：2024 是闰年，2 月有 29 天
TEST_CASE(Calendar_闰年日期正确) {
  CHECK_EQ(WeekdayFromIsoDate("2024-02-29"), std::string("周四"));
  CHECK_EQ(WeekdayFromIsoDate("2024-03-01"), std::string("周五"));
}

// 连续日期必须连续递增（这一条能抓出取模或天数计算的偏移错误）
TEST_CASE(Calendar_连续七天星期连续) {
  const char* expected[] = {"周三", "周四", "周五", "周六",
                            "周日", "周一", "周二"};
  const char* dates[] = {"2026-09-30", "2026-10-01", "2026-10-02", "2026-10-03",
                         "2026-10-04", "2026-10-05", "2026-10-06"};
  for (int i = 0; i < 7; ++i) {
    CHECK_EQ(WeekdayFromIsoDate(dates[i]), std::string(expected[i]));
  }
}

TEST_CASE(Calendar_跨月跨年边界) {
  // 2026-12-31 是周四 -> 2027-01-01 是周五
  CHECK_EQ(WeekdayFromIsoDate("2026-12-31"), std::string("周四"));
  CHECK_EQ(WeekdayFromIsoDate("2027-01-01"), std::string("周五"));
}

TEST_CASE(Calendar_非法日期返回空串) {
  CHECK_EQ(WeekdayFromIsoDate(""), std::string(""));
  CHECK_EQ(WeekdayFromIsoDate("2026-9-30"), std::string(""));   // 缺前导零
  CHECK_EQ(WeekdayFromIsoDate("2026/09/30"), std::string(""));  // 分隔符错
  CHECK_EQ(WeekdayFromIsoDate("2026-13-01"), std::string(""));  // 月份越界
  CHECK_EQ(WeekdayFromIsoDate("abcd-ef-gh"), std::string(""));  // 非数字
}

TEST_CASE(Calendar_短日期截断) {
  CHECK_EQ(ShortDate("2026-09-30"), std::string("09-30"));
  CHECK_EQ(ShortDate("bad"), std::string("bad"));  // 原样返回
}

// ---------------- 中文日期 ----------------

TEST_CASE(ChineseDate_常规日期) {
  CHECK_EQ(ChineseDate("2026-09-30"), std::string("09月30日"));
  CHECK_EQ(ChineseDate("2026-10-07"), std::string("10月07日"));
  CHECK_EQ(ChineseDate("2026-01-01"), std::string("01月01日"));
  // 前导零要保留：版面上日期是固定宽度的槽位，去掉前导零会让它左右跳动
  CHECK_EQ(ChineseDate("2026-12-05"), std::string("12月05日"));
}

TEST_CASE(ChineseDate_非法输入返回空串) {
  CHECK_EQ(ChineseDate(""), std::string(""));
  CHECK_EQ(ChineseDate("2026-9-30"), std::string(""));
  CHECK_EQ(ChineseDate("2026/09/30"), std::string(""));
  CHECK_EQ(ChineseDate("20260930"), std::string(""));
}

// ---------------- 空气质量短标 ----------------

TEST_CASE(AqiShortLabel_官方六档) {
  // 官方 category 原文来自和风天气 airquality 接口的 category 字段
  CHECK_EQ(AqiShortLabel("优"), std::string("优"));
  CHECK_EQ(AqiShortLabel("良"), std::string("良"));
  CHECK_EQ(AqiShortLabel("轻度污染"), std::string("轻度"));
  CHECK_EQ(AqiShortLabel("中度污染"), std::string("中度"));
  CHECK_EQ(AqiShortLabel("重度污染"), std::string("重度"));
  CHECK_EQ(AqiShortLabel("严重污染"), std::string("严重"));
}

TEST_CASE(AqiShortLabel_前缀匹配不误伤) {
  // "良"与"良 好"之类：只认官方写法，不认识的按截断处理。
  // 这条同时守住一个真实踩过的坑 —— 用首字节匹配时，UTF-8 的
  // "晴"与"沙"首字节相同，会把不同字混为一谈。
  CHECK_EQ(AqiShortLabel("轻度"), std::string("轻度"));  // 已经是短标
  CHECK_EQ(AqiShortLabel(""), std::string(""));
  CHECK_EQ(AqiShortLabel("未知档位"), std::string("未知"));  // 截前 2 字
}

TEST_CASE(AqiShortLabel_长度不超过两个汉字) {
  // 徽章只有 38px 宽，短标必须真的短。
  for (const char* c : {"优", "良", "轻度污染", "中度污染", "重度污染",
                        "严重污染", "无法识别的档位"}) {
    const std::string out = AqiShortLabel(c);
    CHECK(out.size() <= 6);  // 最多 2 个 UTF-8 汉字
  }
}

// ---------------- 实时天气页：时钟 ----------------
//
// 时钟与天气内容分开注入（变化率差三个数量级）。这里覆盖时钟这一路的纯逻辑。

TEST_CASE(ClockView_日期时间拆分) {
  const auto data = FormatClockView("2026-09-30", "15:42:08", 250);
  CHECK_EQ(data.date, std::string("09月30日"));
  CHECK_EQ(data.weekday, std::string("周三"));
  CHECK_EQ(data.hhmm, std::string("15:42"));  // 不含秒
  CHECK_EQ(data.seconds, std::string("08"));  // 秒单独一个槽位
  CHECK_EQ(data.ms_into_second, 250u);
}

// 秒内位置是页面把刷新对齐到秒边界的唯一依据，不能在格式化时被丢掉。
TEST_CASE(ClockView_秒内位置透传) {
  CHECK_EQ(FormatClockView("2026-09-30", "15:42:08", 0).ms_into_second, 0u);
  CHECK_EQ(FormatClockView("2026-09-30", "15:42:08", 999).ms_into_second, 999u);
}

TEST_CASE(ClockView_时间未同步时日期与时间为空) {
  const auto data = FormatClockView("", "", 0);
  CHECK_EQ(data.date, std::string(""));
  CHECK_EQ(data.weekday, std::string(""));
  CHECK_EQ(data.hhmm, std::string(""));
  CHECK_EQ(data.seconds, std::string(""));
}

TEST_CASE(ClockView_时间格式异常时不崩) {
  // 不足 5 字符：时:分原样给出，秒为空
  const auto short_time = FormatClockView("", "15", 0);
  CHECK_EQ(short_time.hhmm, std::string("15"));
  CHECK_EQ(short_time.seconds, std::string(""));

  // 有秒但没有冒号
  const auto odd = FormatClockView("", "15-42-08", 0);
  CHECK_EQ(odd.hhmm, std::string("15-42"));
  CHECK_EQ(odd.seconds, std::string("08"));
}

// ---------------- 实时天气页：刷新周期对齐 ----------------
//
// 这一段是"秒位看起来准不准"的全部算术，而且无法在实机上精确观测，
// 所以必须在主机上把性质验穿，而不是只看几个样例。
//
// 注意这里验的是**算出来的周期**：它是"瞄准边界之后"的目标值。真实触发时刻
// 还会被 lv_tick 的步进再量化一次（见 ClockRefreshPeriodMs 的说明），
// 那一层不在本函数的契约里，也不影响"相位不累积"这个结论。

// 目标：瞄准秒边界**之后**，且超出的部分不超过一格。
//   * 提前触发会显示上一个秒值，等于白跑一轮；
//   * 超出超过一格说明取整方式错了。
TEST_CASE(ClockRefresh_目标落点位于秒边界之后且不超过一格) {
  int failures = 0;
  for (uint32_t grid : {1u, 2u, 5u, 10u}) {
    for (uint32_t ms = 0; ms < 1000; ++ms) {
      const uint32_t period = ClockRefreshPeriodMs(ms, grid);
      const uint32_t after_boundary = period - (1000 - ms);
      if (period % grid != 0) {
        ++failures;  // 没落在 lv_tick 能表示的格点上
      }
      if (after_boundary < 1 || after_boundary > grid) {
        ++failures;
      }
    }
  }
  CHECK_EQ(failures, 0);
}

// 样例锚点：便于出问题时一眼看出是哪一侧错了。
TEST_CASE(ClockRefresh_样例) {
  // 正好在边界上：等满一秒，再往后一格
  CHECK_EQ(ClockRefreshPeriodMs(0, 5), 1005u);
  // 距边界只剩 1ms：只需等 1ms 之后的第一个格点，即 5
  CHECK_EQ(ClockRefreshPeriodMs(999, 5), 5u);
  // 到边界 750ms，本身就在格点上：再加一格
  CHECK_EQ(ClockRefreshPeriodMs(250, 5), 755u);
  // 网格为 1 时余量最小，但仍要**严格落在边界之后**：
  // 恰好落在边界上是不安全的（取样可能仍读到上一个秒值），所以再加一格。
  CHECK_EQ(ClockRefreshPeriodMs(0, 1), 1001u);
  CHECK_EQ(ClockRefreshPeriodMs(999, 1), 2u);
  // 网格为 0 无从对齐：退回固定一秒
  CHECK_EQ(ClockRefreshPeriodMs(250, 0), 1000u);
  // 越界输入取模，不能算出荒谬的周期
  CHECK_EQ(ClockRefreshPeriodMs(1250, 5), ClockRefreshPeriodMs(250, 5));
}

// ---------------- 实时天气页：天气内容 ----------------

TEST_CASE(WeatherPage_无城市查询结果时回退到配置地名) {
  WeatherStore store;
  const auto data = FormatWeatherPage(store.Snapshot(), "杭州");
  CHECK_EQ(data.city, std::string("杭州"));
}

TEST_CASE(WeatherPage_优先用name命中的地名) {
  WeatherStore store;
  {
    weather::CityLookup city;
    city.name = "萧山";  // 用户搜的是区
    city.adm2 = "杭州";  // adm2 仍是地级市，但不再优先显示
    store.SetCityLookup(city);
    store.MarkReady(weather::DataSource::kCityLookup, 1);
  }
  const auto data = FormatWeatherPage(store.Snapshot(), "配置地名");
  // 显示 name：它是搜索实际命中的地名，也正是用户想看的"我在哪"。
  // 另一个理由是版面 —— 标签槽位只有 48px（3 个汉字），官方城市列表里
  // adm2 有 22.4% 的取值超宽，name 只有 2.4%（见 weather_types.h）。
  CHECK_EQ(data.city, std::string("萧山"));
}

TEST_CASE(WeatherPage_name缺失时回退到adm2) {
  WeatherStore store;
  {
    weather::CityLookup city;
    city.adm2 = "杭州";  // name 留空：接口异常或直接构造时可能出现
    store.SetCityLookup(city);
    store.MarkReady(weather::DataSource::kCityLookup, 1);
  }
  const auto data = FormatWeatherPage(store.Snapshot(), "配置地名");
  CHECK_EQ(data.city, std::string("杭州"));
}

TEST_CASE(WeatherPage_adm2缺失时回退到name) {
  WeatherStore store;
  {
    weather::CityLookup city;
    city.name = "萧山";
    // adm2 留空 —— 解析器会把 name 填进 adm2，这里模拟直接构造的情况
    store.SetCityLookup(city);
    store.MarkReady(weather::DataSource::kCityLookup, 1);
  }
  const auto data = FormatWeatherPage(store.Snapshot(), "配置地名");
  CHECK_EQ(data.city, std::string("萧山"));
}

TEST_CASE(WeatherPage_完整的就绪数据) {
  WeatherStore store;
  {
    weather::WeatherNow now;
    now.text = "中到大雨";
    now.icon = "305";
    now.temp = "25";
    now.feelsLike = "27";
    now.humidity = "84";
    now.windDir = "东风";
    now.windScale = "3";
    now.vis = "6";
    store.SetCurrentConditions(now);
    store.MarkReady(weather::DataSource::kCurrentConditions, 1);

    weather::AirQualityNow air;
    air.aqi = "42";
    air.category = "优";
    store.SetAirQuality(air);
    store.MarkReady(weather::DataSource::kAirQuality, 1);
  }
  const auto data = FormatWeatherPage(store.Snapshot(), "杭州");

  // 大字用分类，小字用官方完整名 —— 两者都要有，信息不丢
  CHECK_EQ(data.weather_category, std::string("雨"));
  CHECK_EQ(data.weather_text, std::string("中到大雨"));
  CHECK_EQ(data.temperature, std::string("25℃"));
  // 滚动信息条：四段用 " · " 连接
  CHECK_EQ(data.ticker,
           std::string("东风 3级 · 体感 27℃ · 能见度 6km · 湿度 84%"));
  CHECK_EQ(data.aqi_category, std::string("优"));
  CHECK_EQ(data.aqi_short, std::string("优"));
  CHECK_EQ(data.status, std::string(""));  // 就绪时无状态提示
}

TEST_CASE(WeatherPage_信息条缺失字段自动跳过) {
  WeatherStore store;
  {
    weather::WeatherNow now;
    now.text = "晴";
    now.icon = "100";
    now.temp = "25";
    // 只给湿度和风向，体感与能见度留空
    now.humidity = "50";
    now.windDir = "西北风";
    now.windScale = "2";
    store.SetCurrentConditions(now);
    store.MarkReady(weather::DataSource::kCurrentConditions, 1);
  }
  const auto data = FormatWeatherPage(store.Snapshot(), "杭州");
  // 不能出现 " ·  · " 这样的空段
  CHECK_EQ(data.ticker, std::string("西北风 2级 · 湿度 50%"));
}

TEST_CASE(WeatherPage_信息条全空时为空串) {
  WeatherStore store;
  {
    weather::WeatherNow now;
    now.text = "晴";
    now.icon = "100";
    now.temp = "25";
    store.SetCurrentConditions(now);
    store.MarkReady(weather::DataSource::kCurrentConditions, 1);
  }
  const auto data = FormatWeatherPage(store.Snapshot(), "杭州");
  CHECK_EQ(data.ticker, std::string(""));
}

TEST_CASE(WeatherPage_表外icon不给出分类但保留完整名) {
  WeatherStore store;
  {
    weather::WeatherNow now;
    now.text = "热";
    now.icon = "900";  // 官方定义是"热"，不属于任何一类天气现象
    now.temp = "38";
    store.SetCurrentConditions(now);
    store.MarkReady(weather::DataSource::kCurrentConditions, 1);
  }
  const auto data = FormatWeatherPage(store.Snapshot(), "杭州");
  // 分类为空 -> 页面会把完整 text 放进大字位；绝不能填一个"晴"上去
  CHECK_EQ(data.weather_category, std::string(""));
  CHECK_EQ(data.weather_text, std::string("热"));
  CHECK_EQ(data.temperature, std::string("38℃"));
}

// 关键：**从来没有数据**时必须给出提示，而不是显示空白或半截数据。
// 注意前提是"没有数据"—— 有过数据就不该走这条路，见下面的刷新用例。
TEST_CASE(WeatherPage_从未有数据时给出状态而非空白) {
  WeatherStore store;
  store.MarkSyncing(weather::DataSource::kCurrentConditions);
  store.MarkFailed(weather::DataSource::kAirQuality, "boom", 1);

  const auto data = FormatWeatherPage(store.Snapshot(), "杭州");

  CHECK_EQ(data.weather_category, std::string(""));
  CHECK_EQ(data.weather_text, std::string(""));
  CHECK_EQ(data.temperature, std::string(""));
  CHECK_EQ(data.ticker, std::string(""));
  CHECK_EQ(data.status, std::string("天气同步中"));  // 缺的是天气，说的就是天气
}

// 首次同步就失败：此时确实没有任何数据可显示，必须把原因说出来。
TEST_CASE(WeatherPage_首次同步失败时给出原因) {
  WeatherStore store;
  store.MarkFailed(weather::DataSource::kCurrentConditions, "boom", 1);

  const auto data = FormatWeatherPage(store.Snapshot(), "杭州");

  CHECK_EQ(data.status, std::string("同步失败"));
  CHECK_EQ(data.temperature, std::string(""));
}

// ---------------- 实时天气页：刷新期间不得改变显示 ----------------
//
// 这一组是**本次改动的规格**：页面用 data.status 是否为空当作门槛，所以只要
// status 为空、且各字段与上次相同，屏幕上就不会有任何变化。
//
// 背景：每次刷新都是 MarkSyncing → HTTP（超时上限 30 秒）→ SetCurrentConditions
// → MarkReady，而 MarkSyncing / MarkFailed 都不动数据字段。旧实现拿"同步状态"
// 当门槛，于是刷新期间会把天气擦掉换成一句提示 —— 数据明明还在。

namespace {

// 造一份"已经成功同步过"的 store。
void FillReadyWeather(WeatherStore& store) {
  weather::WeatherNow now;
  now.text = "中到大雨";
  now.icon = "305";
  now.temp = "25";
  now.humidity = "84";
  now.windDir = "东风";
  now.windScale = "3";
  store.SetCurrentConditions(now);
  store.MarkReady(weather::DataSource::kCurrentConditions, 1);
}

}  // namespace

TEST_CASE(WeatherPage_刷新期间保留旧数据不变) {
  WeatherStore store;
  FillReadyWeather(store);
  const auto before = FormatWeatherPage(store.Snapshot(), "杭州");

  store.MarkSyncing(weather::DataSource::kCurrentConditions);  // 开始刷新
  const auto during = FormatWeatherPage(store.Snapshot(), "杭州");

  // 刷新期间页面必须一个字都不变：status 为空（页面据此不显示提示），
  // 各字段与刷新前逐项相同。
  CHECK_EQ(during.status, std::string(""));
  CHECK_EQ(during.weather_category, before.weather_category);
  CHECK_EQ(during.weather_text, before.weather_text);
  CHECK_EQ(during.temperature, before.temperature);
  CHECK_EQ(during.ticker, before.ticker);
  CHECK_EQ(during.city, before.city);
}

TEST_CASE(WeatherPage_刷新失败后仍保留旧数据) {
  WeatherStore store;
  FillReadyWeather(store);

  store.MarkSyncing(weather::DataSource::kCurrentConditions);
  store.MarkFailed(weather::DataSource::kCurrentConditions, "boom", 2);

  const auto data = FormatWeatherPage(store.Snapshot(), "杭州");

  // 一次瞬时网络失败不该让天气消失**一整个周期**（30 分钟）——
  // 旧数据比一句"同步失败"有用得多。
  CHECK_EQ(data.status, std::string(""));
  CHECK_EQ(data.weather_category, std::string("雨"));
  CHECK_EQ(data.temperature, std::string("25℃"));
}

TEST_CASE(WeatherPage_刷新成功后换成新数据) {
  WeatherStore store;
  FillReadyWeather(store);

  store.MarkSyncing(weather::DataSource::kCurrentConditions);
  {
    weather::WeatherNow fresh;
    fresh.text = "晴";
    fresh.icon = "100";
    fresh.temp = "31";
    store.SetCurrentConditions(fresh);
    store.MarkReady(weather::DataSource::kCurrentConditions, 2);
  }

  const auto data = FormatWeatherPage(store.Snapshot(), "杭州");

  // 成功即一次性换成新值（store 是锁内整体替换，不存在半新半旧）。
  CHECK_EQ(data.status, std::string(""));
  CHECK_EQ(data.weather_category, std::string("晴"));
  CHECK_EQ(data.temperature, std::string("31℃"));
}

// 空气质量与天气是两路独立数据。AQI 自己刷新时**不能**动天气区：
// 旧实现会把 AQI 的状态填进 data.status，于是每 60 分钟天气大字就消失一次，
// 提示文案还写成"天气同步中"。
TEST_CASE(WeatherPage_空气质量刷新不影响天气区) {
  WeatherStore store;
  FillReadyWeather(store);
  store.MarkSyncing(weather::DataSource::kAirQuality);

  const auto data = FormatWeatherPage(store.Snapshot(), "杭州");

  CHECK_EQ(data.status, std::string(""));  // 天气区不该出现任何提示
  CHECK_EQ(data.weather_category, std::string("雨"));
  CHECK_EQ(data.temperature, std::string("25℃"));
  CHECK_EQ(data.aqi_category, std::string(""));  // 还没有 AQI 数据，徽章不显示
}

TEST_CASE(WeatherPage_空气质量刷新期间徽章不消失) {
  WeatherStore store;
  FillReadyWeather(store);
  {
    weather::AirQualityNow air;
    air.aqi = "42";
    air.category = "优";
    store.SetAirQuality(air);
    store.MarkReady(weather::DataSource::kAirQuality, 1);
  }

  store.MarkSyncing(weather::DataSource::kAirQuality);  // 开始刷新 AQI
  const auto during = FormatWeatherPage(store.Snapshot(), "杭州");

  // 徽章由数据决定，所以刷新期间照旧显示旧值。
  CHECK_EQ(during.aqi_category, std::string("优"));
  CHECK_EQ(during.aqi_short, std::string("优"));
  CHECK_EQ(during.status, std::string(""));

  // 失败也一样：不因为一次失败就把徽章摘掉。
  store.MarkFailed(weather::DataSource::kAirQuality, "boom", 2);
  const auto after = FormatWeatherPage(store.Snapshot(), "杭州");
  CHECK_EQ(after.aqi_short, std::string("优"));
  CHECK_EQ(after.status, std::string(""));
}

// ---------------- 未来天气页 ----------------

TEST_CASE(Forecast_从今天开始含当天) {
  WeatherStore store;
  {
    weather::DailyForecast daily{};
    daily[0].fxDate = "2026-09-30";  // 今天：**要**出现在第一行
    daily[0].textDay = "雾";
    daily[0].tempMin = "23";
    daily[0].tempMax = "29";
    daily[1].fxDate = "2026-10-01";
    daily[1].textDay = "小雨";
    daily[1].tempMin = "22";
    daily[1].tempMax = "28";
    daily[2].fxDate = "2026-10-02";
    daily[2].textDay = "多云";
    daily[2].tempMin = "21";
    daily[2].tempMax = "27";
    store.SetDailyForecast(daily);
    store.MarkReady(weather::DataSource::kDailyForecast, 1);
  }

  // 列表从今天（09-30）开始：它的星期列写"今天"，温度区间是实时天气页
  // 没有的信息（那边显示的是当前温度）。
  const auto data = FormatForecast(store.Snapshot(), "2026-09-30");
  CHECK_EQ(data.rows.size(), 3u);  // 今天 + 两天有数据
  CHECK_EQ(data.rows[0].date, std::string("09-30"));
  CHECK_EQ(data.rows[0].weekday, std::string("今天"));
  CHECK_EQ(data.rows[0].text, std::string("雾"));
  CHECK_EQ(data.rows[0].temp_range, std::string("23/29℃"));
  CHECK_EQ(data.rows[1].date, std::string("10-01"));
  CHECK_EQ(data.rows[1].weekday, std::string("周四"));
  CHECK_EQ(data.rows[1].text, std::string("小雨"));
  CHECK_EQ(data.rows[1].temp_range, std::string("22/28℃"));
  CHECK_EQ(data.rows[2].date, std::string("10-02"));
  CHECK_EQ(data.status, std::string(""));
}

TEST_CASE(Forecast_最多七行且从今天开始) {
  WeatherStore store;
  {
    weather::DailyForecast daily{};
    for (std::size_t i = 0; i < weather::kDailyCount; ++i) {
      daily[i].fxDate = "2026-10-0" + std::to_string(i + 1);
      daily[i].textDay = "晴";
      daily[i].tempMin = "20";
      daily[i].tempMax = "30";
    }
    store.SetDailyForecast(daily);
    store.MarkReady(weather::DataSource::kDailyForecast, 1);
  }
  // 接口给 7 天（今天起），页面就显示 7 行 —— 与标题「7日预报」一致
  const auto data = FormatForecast(store.Snapshot(), "2026-10-01");
  CHECK_EQ(data.rows.size(), 7u);
  CHECK_EQ(data.rows.front().date, std::string("10-01"));
}

TEST_CASE(Forecast_今天那一行标为今天) {
  WeatherStore store;
  {
    weather::DailyForecast daily{};
    for (std::size_t i = 0; i < weather::kDailyCount; ++i) {
      daily[i].fxDate = "2026-10-0" + std::to_string(i + 1);
      daily[i].textDay = "晴";
    }
    store.SetDailyForecast(daily);
    store.MarkReady(weather::DataSource::kDailyForecast, 1);
  }
  const auto data = FormatForecast(store.Snapshot(), "2026-10-03");
  // 10-03 是今天：它的星期列写"今天"，其余照常写星期
  CHECK_EQ(data.rows.size(), 5u);  // 10-01 10-02 已过去，被丢掉
  CHECK_EQ(data.rows[0].date, std::string("10-03"));
  CHECK_EQ(data.rows[0].weekday, std::string("今天"));
  CHECK_EQ(data.rows[1].date, std::string("10-04"));
  CHECK(data.rows[1].weekday != std::string("今天"));
  CHECK(data.rows[1].weekday.rfind("周", 0) == 0);
}

TEST_CASE(Forecast_丢掉已经过去的日子) {
  WeatherStore store;
  {
    weather::DailyForecast daily{};
    for (std::size_t i = 0; i < weather::kDailyCount; ++i) {
      daily[i].fxDate = "2026-10-0" + std::to_string(i + 1);
    }
    store.SetDailyForecast(daily);
    store.MarkReady(weather::DataSource::kDailyForecast, 1);
  }
  // 数据比今天旧（7 日预报每 6 小时才同步，跨过午夜就会差一天）：
  // 前三天已经过去，应当被丢掉而不是当作"今天"标出来。
  const auto data = FormatForecast(store.Snapshot(), "2026-10-04");
  CHECK_EQ(data.rows.size(), 4u);
  CHECK_EQ(data.rows.front().date, std::string("10-04"));
  CHECK_EQ(data.rows.front().weekday, std::string("今天"));
}

TEST_CASE(Forecast_日期未知时不做判断) {
  WeatherStore store;
  {
    weather::DailyForecast daily{};
    for (std::size_t i = 0; i < weather::kDailyCount; ++i) {
      daily[i].fxDate = "2026-10-0" + std::to_string(i + 1);
    }
    store.SetDailyForecast(daily);
    store.MarkReady(weather::DataSource::kDailyForecast, 1);
  }
  // 时间未同步时 net::ReadLocalClock().date 返回空串：
  // 此时既不标"今天"，也不丢任何行 —— 宁可少一层修饰，不能凭猜测丢数据。
  const auto data = FormatForecast(store.Snapshot(), "");
  CHECK_EQ(data.rows.size(), 7u);
  for (const auto& row : data.rows) {
    CHECK(row.weekday != std::string("今天"));
  }
}

TEST_CASE(Forecast_未就绪时给出状态) {
  WeatherStore store;
  store.MarkFailed(weather::DataSource::kDailyForecast, "boom", 1);

  const auto data = FormatForecast(store.Snapshot(), "2026-10-01");
  CHECK(data.rows.empty());
  CHECK_EQ(data.status, std::string("同步失败"));
}

TEST_CASE(Forecast_无有效日期时给出提示) {
  WeatherStore store;
  store.MarkReady(weather::DataSource::kDailyForecast, 1);  // 就绪但没有日期

  const auto data = FormatForecast(store.Snapshot(), "2026-10-01");
  CHECK(data.rows.empty());
  CHECK_EQ(data.status, std::string("暂无预报数据"));
}

// ---------------- 未来天气页：刷新期间不得改变显示 ----------------
//
// 与实时天气页同一类问题：门槛若是"同步状态"，每 6 小时一次的刷新会把已有
// 预报擦掉；而这一页的同步周期是 6 小时，**失败时"同步失败"要挂 6 小时**。

namespace {

// 造一份"已经成功同步过"的 7 日预报。
void FillReadyForecast(WeatherStore& store) {
  weather::DailyForecast daily{};
  daily[0].fxDate = "2026-10-01";
  daily[0].textDay = "雾";
  daily[0].tempMin = "23";
  daily[0].tempMax = "29";
  daily[1].fxDate = "2026-10-02";
  daily[1].textDay = "小雨";
  daily[1].tempMin = "22";
  daily[1].tempMax = "28";
  store.SetDailyForecast(daily);
  store.MarkReady(weather::DataSource::kDailyForecast, 1);
}

}  // namespace

TEST_CASE(Forecast_刷新期间保留旧预报不变) {
  WeatherStore store;
  FillReadyForecast(store);
  const auto before = FormatForecast(store.Snapshot(), "2026-10-01");

  store.MarkSyncing(weather::DataSource::kDailyForecast);
  const auto during = FormatForecast(store.Snapshot(), "2026-10-01");

  CHECK_EQ(during.status, std::string(""));  // 页面据此不显示提示
  CHECK_EQ(during.rows.size(), before.rows.size());
  CHECK_EQ(during.rows[0].date, before.rows[0].date);
  CHECK_EQ(during.rows[0].text, before.rows[0].text);
  CHECK_EQ(during.rows[1].temp_range, before.rows[1].temp_range);
}

TEST_CASE(Forecast_刷新失败后仍保留旧预报) {
  WeatherStore store;
  FillReadyForecast(store);

  store.MarkSyncing(weather::DataSource::kDailyForecast);
  store.MarkFailed(weather::DataSource::kDailyForecast, "boom", 2);

  const auto data = FormatForecast(store.Snapshot(), "2026-10-01");

  // 一次失败之后"同步失败"要等到下一个周期（6 小时）才有机会消失，
  // 所以这里绝不能把已有预报换掉。
  CHECK_EQ(data.status, std::string(""));
  CHECK_EQ(data.rows.size(), 2u);
  CHECK_EQ(data.rows[0].text, std::string("雾"));
}

TEST_CASE(Forecast_刷新成功后换成新预报) {
  WeatherStore store;
  FillReadyForecast(store);

  {
    weather::DailyForecast fresh{};
    fresh[0].fxDate = "2026-10-01";
    fresh[0].textDay = "晴";
    fresh[0].tempMin = "20";
    fresh[0].tempMax = "26";
    store.SetDailyForecast(fresh);
    store.MarkReady(weather::DataSource::kDailyForecast, 2);
  }

  const auto data = FormatForecast(store.Snapshot(), "2026-10-01");
  CHECK_EQ(data.status, std::string(""));
  CHECK_EQ(data.rows.size(), 1u);
  CHECK_EQ(data.rows[0].text, std::string("晴"));
  CHECK_EQ(data.rows[0].temp_range, std::string("20/26℃"));
}

// 数据全都比今天旧：行被丢光是**结果**，不是"刷新中"—— 此时才该给提示。
// 而只要还有一行可显示，状态就不该参与渲染（见上面的刷新用例）。
TEST_CASE(Forecast_数据全部过期时才给提示) {
  WeatherStore store;
  FillReadyForecast(store);
  store.MarkSyncing(weather::DataSource::kDailyForecast);  // 而且正在刷新

  const auto data = FormatForecast(store.Snapshot(), "2026-10-05");

  CHECK(data.rows.empty());                          // 10-01 / 10-02 都已过去
  CHECK_EQ(data.status, std::string("天气同步中"));  // 没东西可看，才解释原因
}

// ---------------- 运行期状态 ----------------

TEST_CASE(StartupStatus_阶段名与快照) {
  StartupStatus status;
  CHECK(status.Snapshot().phase == StartupPhase::kBoot);

  status.Set(StartupPhase::kConnectingWifi, "MyWiFi");
  const auto snapshot = status.Snapshot();
  CHECK(snapshot.phase == StartupPhase::kConnectingWifi);
  CHECK_EQ(snapshot.detail, std::string("MyWiFi"));
  CHECK_EQ(snapshot.sequence, 1u);

  status.Set(StartupPhase::kReady);
  CHECK_EQ(status.Snapshot().sequence, 2u);
  CHECK_EQ(status.Snapshot().detail, std::string(""));  // 新阶段清空说明
}

TEST_CASE(StartupStatus_每个阶段都有可显示的名字) {
  for (const StartupPhase phase :
       {StartupPhase::kBoot, StartupPhase::kLoadingConfig,
        StartupPhase::kWaitingForConfig, StartupPhase::kConnectingWifi,
        StartupPhase::kSyncingTime, StartupPhase::kSyncingWeather,
        StartupPhase::kReady, StartupPhase::kFailed}) {
    const std::string name = StartupPhaseName(phase);
    CHECK(!name.empty());
    CHECK(name != "未知");
  }
}

TEST_CASE(PortalStatus_发布与读取) {
  PortalStatus status;
  CHECK(!status.Snapshot().running);

  status.Publish(true, "DIDA-1A2B", "12345678", "192.168.4.1");
  const auto snapshot = status.Snapshot();
  CHECK(snapshot.running);
  CHECK_EQ(snapshot.ssid, std::string("DIDA-1A2B"));
  CHECK_EQ(snapshot.password, std::string("12345678"));
  CHECK_EQ(snapshot.ip, std::string("192.168.4.1"));
}

// 快照必须是拷贝：读侧在锁外改它不能影响内部状态
TEST_CASE(PortalStatus_快照是独立拷贝) {
  PortalStatus status;
  status.Publish(true, "DIDA-0001", "pw", "192.168.4.1");

  auto copy = status.Snapshot();
  copy.ssid = "被篡改";

  CHECK_EQ(status.Snapshot().ssid, std::string("DIDA-0001"));
}

// 并发回归：写侧（启动任务）与读侧（UI 任务）同时访问不得有竞争。
// 与 WeatherStore 一样，这条在 --tsan 下被真正验证。
TEST_CASE(RuntimeStatus_跨任务读写无竞争) {
  StartupStatus status;
  PortalStatus portal;

  constexpr int kIterations = 20000;
  std::thread writer([&]() {
    for (int i = 0; i < kIterations; ++i) {
      status.Set(
          i % 2 == 0 ? StartupPhase::kConnectingWifi
                     : StartupPhase::kSyncingWeather,
          "detail-with-some-length-to-force-heap-alloc-" + std::to_string(i));
      portal.Publish(true, "DIDA-1A2B-" + std::to_string(i),
                     "password-12345678", "192.168.4.1");
    }
  });

  std::thread reader([&]() {
    for (int i = 0; i < kIterations; ++i) {
      const auto s = status.Snapshot();
      const auto p = portal.Snapshot();
      // 触碰字符串内容，确保读到的是真正的拷贝而不是空壳
      volatile std::size_t sink = s.detail.size() + p.ssid.size();
      (void)sink;
    }
  });

  writer.join();
  reader.join();
  CHECK_EQ(status.Snapshot().sequence, static_cast<uint32_t>(kIterations));
}
