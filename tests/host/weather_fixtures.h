// 和风天气各端点响应的测试样本。
//
// 关键点：这些样本**刻意保留真实 API 的类型差异** —— 部分字段是字符串，
// 部分字段是数字（如 indexes[].aqi、pollutants[].concentration.value）。
// 这正是用 cJSON 手工解析时最容易出错的地方。

#pragma once

#include <string>

namespace fixtures {

// ---------------- 城市查询 ----------------
inline std::string CityLookupOk() {
  return R"({
    "code": "200",
    "location": [
      {
        "name": "余杭",
        "id": "101210101",
        "lat": "30.42",
        "lon": "120.30",
        "adm2": "杭州",
        "adm1": "浙江",
        "country": "中国"
      }
    ]
  })";
}

inline std::string CityLookupEmpty() {
  return R"({"code": "200", "location": []})";
}

inline std::string CityLookupNoId() {
  return R"({"code": "200", "location": [{"name": "余杭"}]})";
}

// ---------------- 实时天气 ----------------
inline std::string WeatherNowOk() {
  return R"({
    "code": "200",
    "updateTime": "2021-02-16T13:02+08:00",
    "now": {
      "obsTime": "2021-02-16T12:50+08:00",
      "temp": "26",
      "feelsLike": "28",
      "icon": "100",
      "text": "晴",
      "wind360": "315",
      "windDir": "西北风",
      "windScale": "3",
      "windSpeed": "15",
      "humidity": "60",
      "precip": "0.0",
      "pressure": "1010",
      "vis": "16",
      "cloud": "0",
      "dew": "17"
    },
    "refer": {"sources": ["QWeather"], "license": ["CC BY-SA 4.0"]}
  })";
}

// 温度是数字而非字符串 —— 真实 API 在某些版本会这样返回
inline std::string WeatherNowNumericTemp() {
  return R"({"code": "200", "now": {"temp": 26, "feelsLike": 28,
            "icon": "100", "text": "晴", "windDir": "西北风",
            "windScale": "3", "humidity": 60, "vis": 16}})";
}

inline std::string WeatherNowMissingNow() {
  return R"({"code": "200", "updateTime": "2021-02-16T13:02+08:00"})";
}

inline std::string WeatherNowNoText() {
  return R"({"code": "200", "now": {"temp": "26", "icon": "100"}})";
}

// ---------------- API 业务码异常 ----------------
inline std::string ApiCode204() { return R"({"code": "204", "location": []})"; }

inline std::string ApiCodeMissing() { return R"({"location": []})"; }

inline std::string InvalidJson() { return R"({"code": "200", "now": {)"; }

// ---------------- 7 日预报 ----------------
inline std::string DailyForecast(int days) {
  std::string daily;
  for (int i = 0; i < days; ++i) {
    if (i > 0) {
      daily += ",";
    }
    const int day = 1 + i;
    daily += R"({"fxDate": "2021-11-)";
    if (day < 10) {
      daily += "0";
    }
    daily += std::to_string(day) +
             R"(", "sunrise": "06:35", "sunset": "17:12",
                 "tempMax": "22", "tempMin": "12",
                 "iconDay": "100", "textDay": "晴",
                 "iconNight": "150", "textNight": "晴"})";
  }
  return R"({"code": "200", "daily": [)" + daily + R"(]})";
}

inline std::string DailyForecastEmpty() {
  return R"({"code": "200", "daily": []})";
}

// ---------------- 空气质量 ----------------
// aqi 与 concentration.value 都是数字（真实 API 形态）
inline std::string AirQualityNumeric() {
  return R"({
    "code": "200",
    "indexes": [
      {
        "code": "cn-mee",
        "name": "AQI",
        "aqi": 45,
        "aqiDisplay": "45",
        "category": "优",
        "primaryPollutant": {"code": "na", "name": "NA"}
      }
    ],
    "pollutants": [
      {"code": "pm2p5", "name": "PM2.5",
       "concentration": {"value": 20, "unit": "μg/m³"}},
      {"code": "pm10", "name": "PM10",
       "concentration": {"value": 38, "unit": "μg/m³"}}
    ]
  })";
}

// 只有 aqi（无 aqiDisplay），验证回退分支
inline std::string AirQualityFallbackAqi() {
  return R"({"code": "200",
            "indexes": [{"aqi": 128, "category": "轻度污染"}],
            "pollutants": []})";
}

// 用中文名 "细颗粒物" 匹配 pm2.5
inline std::string AirQualityChinesePollutantName() {
  return R"({"code": "200",
            "indexes": [{"aqi": 60, "category": "良"}],
            "pollutants": [
              {"code": "pm10", "name": "PM10", "concentration": {"value": 40}},
              {"code": "", "name": "细颗粒物", "concentration": {"value": 25}}
            ]})";
}

inline std::string AirQualityNoIndexes() {
  return R"({"code": "200", "indexes": [], "pollutants": []})";
}

}  // namespace fixtures
