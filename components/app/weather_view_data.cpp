#include "app/weather_view_data.h"

#include <cstdio>
#include <cstdlib>

#include "weather/weather_category.h"

namespace app {
namespace {

// Howard Hinnant 的 days_from_civil：把公历日期转成"距 1970-01-01 的天数"。
// 用它而不用 std::mktime，是因为它不依赖时区/本地化，纯算术、可复现。
int DaysFromCivil(int y, unsigned m, unsigned d) {
  y -= (m <= 2) ? 1 : 0;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

// 索引 0 = 周一。用中文而不是英文缩写：界面已全部中文化，
// 而且中文字宽固定，"周一"比"Mon"更好排版。
const char* const kWeekdayNames[7] = {"周一", "周二", "周三", "周四",
                                      "周五", "周六", "周日"};

// 解析 "YYYY-MM-DD"。成功返回 true。
bool ParseIsoDate(const std::string& iso, int& year, unsigned& month,
                  unsigned& day) {
  if (iso.size() != 10 || iso[4] != '-' || iso[7] != '-') {
    return false;
  }
  for (std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
    if (iso[i] < '0' || iso[i] > '9') {
      return false;
    }
  }
  year = (iso[0] - '0') * 1000 + (iso[1] - '0') * 100 + (iso[2] - '0') * 10 +
         (iso[3] - '0');
  month = static_cast<unsigned>((iso[5] - '0') * 10 + (iso[6] - '0'));
  day = static_cast<unsigned>((iso[8] - '0') * 10 + (iso[9] - '0'));

  if (month < 1 || month > 12 || day < 1 || day > 31) {
    return false;
  }
  return true;
}

std::string DescribeSyncState(const weather::SyncState& state) {
  switch (state.status) {
    case weather::SyncStatus::kIdle:
      return "等待同步";
    case weather::SyncStatus::kSyncing:
      return "天气同步中";
    case weather::SyncStatus::kReady:
      return {};
    case weather::SyncStatus::kFailed:
      return "同步失败";
  }
  return {};
}

// 把 "14:26:37" 拆成 "14:26" 与 "37"。格式不对时 hhmm 原样返回、秒为空。
void SplitTime(const std::string& time_text, std::string& hhmm,
               std::string& seconds) {
  hhmm.clear();
  seconds.clear();
  if (time_text.empty()) {
    return;
  }
  hhmm = time_text.size() >= 5 ? time_text.substr(0, 5) : time_text;
  if (time_text.size() >= 8) {
    seconds = time_text.substr(6, 2);
  }
}

}  // namespace

std::string WeekdayFromIsoDate(const std::string& iso_date) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (!ParseIsoDate(iso_date, year, month, day)) {
    return {};
  }
  // 1970-01-01 是周四。让 0 = 周一：把天数偏移 3 后取模。
  const int days = DaysFromCivil(year, month, day);
  const int index = ((days % 7) + 3 + 7) % 7;
  return kWeekdayNames[index];
}

std::string ShortDate(const std::string& iso_date) {
  if (iso_date.size() != 10) {
    return iso_date;
  }
  return iso_date.substr(5, 5);  // "MM-DD"
}

std::string ChineseDate(const std::string& iso_date) {
  // 复用与星期换算同一套校验：只查长度会让 "2026/09/30" 这种也能通过，
  // 而它会被原样拼成 "09月30日" —— 错得看不出来。
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (!ParseIsoDate(iso_date, year, month, day)) {
    return {};
  }
  // "2026-10-07" -> "10月07日"。用 substr 而不是 strftime：不依赖 locale。
  return iso_date.substr(5, 2) + "月" + iso_date.substr(8, 2) + "日";
}

std::string AqiShortLabel(const std::string& category) {
  // 官方只有 6 档。用**完整前缀**匹配而不是取首字节：
  // UTF-8 里"轻"和"重"的首字节不同，但"优"和"严"之类的判断一旦退化成
  // 逐字节比较就会出错，显式写全更不容易被后续改动破坏。
  // 单字档（优良）返回 3 字节，双字档（轻度..严重）返回 6 字节。
  static constexpr const char* kSingle[] = {"优", "良"};
  static constexpr const char* kDouble[] = {"轻度", "中度", "重度", "严重"};
  for (const char* k : kSingle) {
    if (category.rfind(k, 0) == 0) {
      return std::string(k);
    }
  }
  for (const char* k : kDouble) {
    if (category.rfind(k, 0) == 0) {
      return std::string(k);
    }
  }
  // 认不出的截前 2 字（最多 6 字节），好过空着。
  return category.size() >= 6 ? category.substr(0, 6) : category;
}

ClockViewData FormatClockView(const std::string& date_text,
                              const std::string& time_text,
                              uint32_t ms_into_second) {
  ClockViewData data;
  if (!date_text.empty()) {
    data.date = ChineseDate(date_text);
    data.weekday = WeekdayFromIsoDate(date_text);
  }
  SplitTime(time_text, data.hhmm, data.seconds);
  // 不在这里判断"时间是否为空"：hhmm 为空即代表未同步，页面据此不使用它。
  data.ms_into_second = ms_into_second;
  return data;
}

uint32_t ClockRefreshPeriodMs(uint32_t ms_into_second, uint32_t tick_grid_ms) {
  constexpr uint32_t kSecondMs = 1000;
  if (tick_grid_ms == 0) {
    return kSecondMs;  // 网格为 0 无从对齐，退回固定一秒
  }
  // 契约是 [0,999]；越界值取模，免得算出一个荒谬的周期。
  const uint32_t to_boundary = kSecondMs - (ms_into_second % kSecondMs);
  // 把"到边界的距离"**向下**取整到网格，再加一格：
  // 触发点因此落在边界之后 1..tick_grid_ms 毫秒内，既不会提前（提前会显示
  // 上一个秒值，白跑一轮），也不会多等出去（lv_tick 按 tick_grid_ms 步进，
  // 取整方式决定了这是能表示的最接近的格点）。
  return (to_boundary / tick_grid_ms) * tick_grid_ms + tick_grid_ms;
}

WeatherPageViewData FormatWeatherPage(const store::WeatherSnapshot& snapshot,
                                      const std::string& city_name) {
  WeatherPageViewData data;

  // ---- 地名 ----
  //
  // 优先用 name —— 它是**搜索实际命中的那个地名**（区/县/县级市，也可能是州府），
  // 也就是用户真正想看的"我在哪"。回退到 adm2（地级市），再回退到配置里的地名。
  //
  // 为什么不优先 adm2：城市标签的槽位只有 kTextSlotWidth=48px（3 个汉字），
  // 而 adm2 有 22.4% 的取值超过 3 字（"乌鲁木齐市""乌兰察布市"…），会被省略号
  // 截断；name 的取值有 97.6% 在 3 字以内。字库代价见 charset/city.txt。
  const std::string resolved =
      snapshot.city.name.empty() ? snapshot.city.adm2 : snapshot.city.name;
  data.city = resolved.empty() ? city_name : resolved;

  // ---- 天气 ----
  //
  // 门槛是"store 里有没有数据"，不是"当前同步状态"：每次刷新都会先 MarkSyncing
  // 再走 HTTP（超时上限 30 秒），而 MarkSyncing / MarkFailed **都不动数据字段**。
  // 拿状态当门槛，就会在刷新期间、以及失败后的整个周期把已有天气擦掉换成提示。
  //
  // 判据用 text：解析器在成功路径上强制 text 与 temp 非空（kMissingData 分支），
  // 所以 text 非空 ⟺ 曾经成功同步过。
  const bool has_now = !snapshot.now.text.empty();
  if (has_now) {
    // 大字用分类（最多 2 字），小字用官方完整 text —— 两者都给，不丢信息。
    // 分类取自 icon 代码而不是 text：icon 与语言无关，改 lang 不会让它失效。
    data.weather_category = weather::WeatherCategoryName(snapshot.now.icon);
    data.weather_text = snapshot.now.text;
    data.temperature = snapshot.now.temp + "℃";

    // 滚动信息条。四段用 " · " 连接；缺项自动跳过，
    // 避免出现 "东风 4级 ·  · 能见度 25km" 这样的空段。
    auto append = [&data](const std::string& part) {
      if (part.empty()) {
        return;
      }
      if (!data.ticker.empty()) {
        data.ticker += " · ";
      }
      data.ticker += part;
    };
    if (!snapshot.now.windDir.empty()) {
      append(snapshot.now.windDir + " " + snapshot.now.windScale + "级");
    }
    if (!snapshot.now.feelsLike.empty()) {
      append("体感 " + snapshot.now.feelsLike + "℃");
    }
    if (!snapshot.now.vis.empty()) {
      append("能见度 " + snapshot.now.vis + "km");
    }
    if (!snapshot.now.humidity.empty()) {
      append("湿度 " + snapshot.now.humidity + "%");
    }
  } else {
    // 只有真的没有数据时才解释原因。走到这里说明缺的是天气本身，
    // 所以这条状态文案必然是在说天气。
    data.status = DescribeSyncState(snapshot.now_state);
  }

  // ---- 空气质量 ----
  //
  // 同样不看状态：徽章出不出现，由 store 里有没有 category 决定（category 为空
  // 时短标也是空，页面自然就不显示徽章）。
  //
  // AQI 的状态**不再**参与 data.status：否则空气质量每 60 分钟刷新一次，就会
  // 连带把天气大字换成"天气同步中" —— 而那条文案说的是天气，与实际在同步的
  // 东西根本不是一回事。
  data.aqi_category = snapshot.air_quality.category;
  data.aqi_short = AqiShortLabel(data.aqi_category);

  return data;
}

ForecastViewData FormatForecast(const store::WeatherSnapshot& snapshot,
                                const std::string& today_iso) {
  ForecastViewData data;

  for (std::size_t i = 0; i < weather::kDailyCount; ++i) {
    const weather::WeatherDaily& day = snapshot.daily[i];
    if (day.fxDate.empty()) {
      continue;
    }
    // 数据可能比今天旧（每 6 小时才同步一次，跨过午夜就会差一天）。
    // ISO 日期串可以直接比较大小，用来丢掉已经过去的日子。
    if (!today_iso.empty() && day.fxDate < today_iso) {
      continue;
    }
    ForecastRow row;
    row.date = ShortDate(day.fxDate);
    // 第一条若是今天，就用"今天"代替星期 —— 比让用户自己数更直接。
    row.weekday = (day.fxDate == today_iso) ? std::string("今天")
                                            : WeekdayFromIsoDate(day.fxDate);
    row.text = day.textDay;
    row.category = weather::WeatherCategoryName(day.iconDay);
    row.temp_range = day.tempMin + "/" + day.tempMax + "℃";
    data.rows.push_back(std::move(row));
  }

  // 状态只用来解释"为什么没有行"，**不参与决定要不要渲染已有数据**
  // （同 FormatWeatherPage）。7 日预报每 6 小时才同步一次，拿状态当门槛
  // 意味着刷新期间、以及失败后的整整 6 小时里，已有的预报都会被擦掉。
  if (data.rows.empty()) {
    data.status = DescribeSyncState(snapshot.daily_state);
    if (data.status.empty()) {
      // 状态是 kReady 却没有可用行：要么接口给了空数据，要么拿到的那批
      // 日期都已经过去（设备离线超过一天时会这样）。
      data.status = "暂无预报数据";
    }
  }
  return data;
}

}  // namespace app
