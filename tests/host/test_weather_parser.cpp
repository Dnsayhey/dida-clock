// weather 组件的解析逻辑单测（主机运行，不需硬件）。

#include "test_framework.h"
#include "weather/weather_parser.h"
#include "weather_fixtures.h"

using weather::ParseError;
using weather::ParseResult;

// ---------------- 城市查询 ----------------

TEST_CASE(CityLookup_解析成功) {
  weather::CityLookup city;
  const ParseResult r =
      weather::ParseCityLookup(fixtures::CityLookupOk(), city);

  CHECK(r.ok);
  CHECK_EQ(weather::ParseErrorName(r.error), std::string("none"));
  CHECK_EQ(city.name, std::string("余杭"));
  CHECK_EQ(city.id, std::string("101210101"));
  CHECK_EQ(city.lat, std::string("30.42"));
  CHECK_EQ(city.lon, std::string("120.30"));
}

TEST_CASE(CityLookup_空location判为无数据) {
  weather::CityLookup city;
  const ParseResult r =
      weather::ParseCityLookup(fixtures::CityLookupEmpty(), city);
  CHECK(!r.ok);
  CHECK(r.error == ParseError::kMissingData);
}

TEST_CASE(CityLookup_缺少id判为无数据) {
  weather::CityLookup city;
  const ParseResult r =
      weather::ParseCityLookup(fixtures::CityLookupNoId(), city);
  CHECK(!r.ok);
  CHECK(r.error == ParseError::kMissingData);
}

// ---------------- 实时天气 ----------------

TEST_CASE(WeatherNow_解析全部字段) {
  weather::WeatherNow now;
  const ParseResult r = weather::ParseWeatherNow(fixtures::WeatherNowOk(), now);

  CHECK(r.ok);
  CHECK_EQ(now.temp, std::string("26"));
  CHECK_EQ(now.feelsLike, std::string("28"));
  CHECK_EQ(now.icon, std::string("100"));
  CHECK_EQ(now.text, std::string("晴"));
  CHECK_EQ(now.windDir, std::string("西北风"));
  CHECK_EQ(now.windScale, std::string("3"));
  CHECK_EQ(now.humidity, std::string("60"));
  CHECK_EQ(now.vis, std::string("16"));
}

// 这条是用 cJSON 手工解析时最容易踩的坑：数字字段必须也能取到值。
TEST_CASE(WeatherNow_数字类型字段也能解析) {
  weather::WeatherNow now;
  const ParseResult r =
      weather::ParseWeatherNow(fixtures::WeatherNowNumericTemp(), now);

  CHECK(r.ok);
  CHECK_EQ(now.temp, std::string("26"));
  CHECK_EQ(now.feelsLike, std::string("28"));
  CHECK_EQ(now.humidity, std::string("60"));
  CHECK_EQ(now.vis, std::string("16"));
}

TEST_CASE(WeatherNow_缺少now判为无数据) {
  weather::WeatherNow now;
  const ParseResult r =
      weather::ParseWeatherNow(fixtures::WeatherNowMissingNow(), now);
  CHECK(!r.ok);
  CHECK(r.error == ParseError::kMissingData);
}

TEST_CASE(WeatherNow_缺少text判为无数据) {
  weather::WeatherNow now;
  const ParseResult r =
      weather::ParseWeatherNow(fixtures::WeatherNowNoText(), now);
  CHECK(!r.ok);
  CHECK(r.error == ParseError::kMissingData);
}

// ---------------- 错误分级 ----------------

TEST_CASE(错误分级_业务码非200) {
  weather::CityLookup city;
  const ParseResult r = weather::ParseCityLookup(fixtures::ApiCode204(), city);

  CHECK(!r.ok);
  CHECK(r.error == ParseError::kApiCodeNotOk);
  CHECK_EQ(r.api_code, std::string("204"));
  // 诊断信息里应带上业务码
  CHECK(r.message.find("204") != std::string::npos);
}

TEST_CASE(错误分级_业务码缺失) {
  weather::CityLookup city;
  const ParseResult r =
      weather::ParseCityLookup(fixtures::ApiCodeMissing(), city);
  CHECK(!r.ok);
  CHECK(r.error == ParseError::kApiCodeNotOk);
}

TEST_CASE(错误分级_非法JSON) {
  weather::WeatherNow now;
  const ParseResult r = weather::ParseWeatherNow(fixtures::InvalidJson(), now);
  CHECK(!r.ok);
  CHECK(r.error == ParseError::kInvalidJson);
}

TEST_CASE(错误分级_空输入) {
  weather::WeatherNow now;
  const ParseResult r = weather::ParseWeatherNow("", now);
  CHECK(!r.ok);
  CHECK(r.error == ParseError::kInvalidJson);
}

// ---------------- 7 日预报（边界：不足 7 天 / 多于 7 天）----------------

TEST_CASE(DailyForecast_完整7天) {
  weather::DailyForecast daily;
  const ParseResult r =
      weather::ParseDailyForecast(fixtures::DailyForecast(7), daily);

  CHECK(r.ok);
  CHECK_EQ(daily[0].fxDate, std::string("2021-11-01"));
  CHECK_EQ(daily[0].textDay, std::string("晴"));
  CHECK_EQ(daily[0].tempMax, std::string("22"));
  CHECK_EQ(daily[0].tempMin, std::string("12"));
  CHECK_EQ(daily[6].fxDate, std::string("2021-11-07"));
}

// 不足 7 天：剩余条目必须为空，不能残留旧数据
TEST_CASE(DailyForecast_不足7天时其余为空) {
  weather::DailyForecast daily;
  // 先塞满旧数据，确认解析会整体覆盖
  for (auto& d : daily) {
    d.fxDate = "旧数据";
    d.textDay = "旧数据";
  }

  const ParseResult r =
      weather::ParseDailyForecast(fixtures::DailyForecast(3), daily);

  CHECK(r.ok);
  CHECK_EQ(daily[0].fxDate, std::string("2021-11-01"));
  CHECK_EQ(daily[2].fxDate, std::string("2021-11-03"));
  CHECK_EQ(daily[3].fxDate, std::string(""));
  CHECK_EQ(daily[6].fxDate, std::string(""));
  CHECK_EQ(daily[6].textDay, std::string(""));
}

// 超过 7 天：必须截断，不能越界写
TEST_CASE(DailyForecast_超过7天时截断) {
  weather::DailyForecast daily;
  const ParseResult r =
      weather::ParseDailyForecast(fixtures::DailyForecast(12), daily);

  CHECK(r.ok);
  CHECK_EQ(daily[0].fxDate, std::string("2021-11-01"));
  CHECK_EQ(daily[6].fxDate, std::string("2021-11-07"));
  // std::array 长度固定为 7，越界在类型层面已不可能；这里确认第 7 条未被写坏
  CHECK_EQ(daily[6].textNight, std::string("晴"));
}

TEST_CASE(DailyForecast_空daily判为无数据) {
  weather::DailyForecast daily;
  const ParseResult r =
      weather::ParseDailyForecast(fixtures::DailyForecastEmpty(), daily);
  CHECK(!r.ok);
  CHECK(r.error == ParseError::kMissingData);
}

// ---------------- 空气质量 ----------------

TEST_CASE(AirQuality_数字字段与pm25匹配) {
  weather::AirQualityNow air;
  const ParseResult r =
      weather::ParseAirQualityNow(fixtures::AirQualityNumeric(), air);

  CHECK(r.ok);
  CHECK_EQ(air.aqi, std::string("45"));  // 来自 aqiDisplay
  CHECK_EQ(air.category, std::string("优"));
  CHECK_EQ(air.primaryPollutant, std::string("NA"));
  CHECK_EQ(air.pm2p5, std::string("20"));  // 数字 20 -> "20"
}

TEST_CASE(AirQuality_aqiDisplay缺失时回退aqi) {
  weather::AirQualityNow air;
  const ParseResult r =
      weather::ParseAirQualityNow(fixtures::AirQualityFallbackAqi(), air);

  CHECK(r.ok);
  CHECK_EQ(air.aqi, std::string("128"));
  CHECK_EQ(air.category, std::string("轻度污染"));
}

TEST_CASE(AirQuality_按中文名匹配细颗粒物) {
  weather::AirQualityNow air;
  const ParseResult r = weather::ParseAirQualityNow(
      fixtures::AirQualityChinesePollutantName(), air);

  CHECK(r.ok);
  CHECK_EQ(air.pm2p5, std::string("25"));  // 不是 PM10 的 40
}

TEST_CASE(AirQuality_空indexes判为无数据) {
  weather::AirQualityNow air;
  const ParseResult r =
      weather::ParseAirQualityNow(fixtures::AirQualityNoIndexes(), air);
  CHECK(!r.ok);
  CHECK(r.error == ParseError::kMissingData);
}
