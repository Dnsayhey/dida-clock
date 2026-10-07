// 用**真实抓取的和风天气响应**验证解析逻辑。
//
// 这些 fixture 是通过真实 API 请求（gzip 响应解压后）保存的，见 fixtures/。
// 手工编造的样本容易"刚好符合实现假设"，真实数据的价值在于它会暴露
// 实现里想当然的地方 —— 事实上已经暴露了两个：
//   1. 空气质量端点**没有 code 字段**，一刀切校验业务码会误杀全部合法响应；
//   2. pm2.5 的名字实际是 "PM 2.5"（带空格），而非 "PM2.5"。

#include "fixture_loader.h"
#include "test_framework.h"
#include "weather/weather_parser.h"

using weather::ParseError;
using weather::ParseResult;

// ---------------- 城市查询 ----------------

TEST_CASE(真实响应_城市查询) {
  const std::string json = fixtures::Load("city_lookup.json");
  CHECK(!json.empty());

  weather::CityLookup city;
  const ParseResult r = weather::ParseCityLookup(json, city);

  CHECK(r.ok);
  CHECK_EQ(city.name, std::string("杭州"));
  CHECK_EQ(city.id, std::string("101210101"));
  // 真实返回的经纬度是 5 位小数
  CHECK_EQ(city.lat, std::string("30.24603"));
  CHECK_EQ(city.lon, std::string("120.21079"));
}

// ---------------- 实时天气 ----------------

TEST_CASE(真实响应_实时天气) {
  const std::string json = fixtures::Load("weather_now.json");
  CHECK(!json.empty());

  weather::WeatherNow now;
  const ParseResult r = weather::ParseWeatherNow(json, now);

  CHECK(r.ok);
  CHECK_EQ(now.temp, std::string("25"));
  CHECK_EQ(now.text, std::string("雾"));
  CHECK_EQ(now.icon, std::string("501"));
  CHECK_EQ(now.feelsLike, std::string("27"));
  CHECK_EQ(now.windDir, std::string("东风"));
  CHECK_EQ(now.windScale, std::string("3"));
  CHECK_EQ(now.humidity, std::string("84"));
  CHECK_EQ(now.vis, std::string("6"));
}

// ---------------- 7 日预报 ----------------

TEST_CASE(真实响应_七日预报完整7天) {
  const std::string json = fixtures::Load("daily_forecast.json");
  CHECK(!json.empty());

  weather::DailyForecast daily;
  const ParseResult r = weather::ParseDailyForecast(json, daily);

  CHECK(r.ok);
  // 真实响应正好 7 天，必须全部落到数组里且不越界
  CHECK_EQ(daily[0].fxDate, std::string("2026-09-29"));
  CHECK_EQ(daily[0].textDay, std::string("小雨"));
  CHECK_EQ(daily[0].tempMax, std::string("29"));
  CHECK_EQ(daily[0].tempMin, std::string("23"));
  CHECK_EQ(daily[0].iconDay, std::string("305"));
  CHECK_EQ(daily[6].fxDate, std::string("2026-10-05"));
  CHECK_EQ(daily[6].textDay, std::string("晴"));
}

// ---------------- 空气质量（无 code 字段）----------------

// 这条是最关键的回归：真实空气质量响应没有 code 字段，
// 一刀切要求 code=="200" 会把合法响应判为 kApiCodeNotOk。
TEST_CASE(真实响应_空气质量无code字段也必须解析成功) {
  const std::string json = fixtures::Load("air_quality.json");
  CHECK(!json.empty());
  // 先确认前提：这份真实响应确实没有 code 字段
  CHECK(json.find("\"code\": \"200\"") == std::string::npos);

  weather::AirQualityNow air;
  const ParseResult r = weather::ParseAirQualityNow(json, air);

  CHECK(r.ok);
  CHECK_EQ(air.aqi, std::string("42"));  // aqiDisplay 优先
  CHECK_EQ(air.category, std::string("优"));
  // 真实数据里 primaryPollutant 是 null，必须安全降级为空串而不是崩溃
  CHECK_EQ(air.primaryPollutant, std::string(""));
  // concentration.value 是浮点 29.0 -> 应输出 "29"
  CHECK_EQ(air.pm2p5, std::string("29"));
}

// 名字带空格的边界：真实 name 是 "PM 2.5"，靠 code=="pm2p5" 匹配才能命中
TEST_CASE(真实响应_空气质量pm25靠code匹配而非名字) {
  const std::string json = fixtures::Load("air_quality.json");
  weather::AirQualityNow air;
  const ParseResult r = weather::ParseAirQualityNow(json, air);

  CHECK(r.ok);
  CHECK(!air.pm2p5.empty());
  CHECK_EQ(air.pm2p5, std::string("29"));
}
