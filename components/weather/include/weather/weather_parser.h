#pragma once

#include <string>

#include "weather/weather_types.h"

namespace weather {

// 解析失败的原因分级。
// 只区分"成功/失败"无法判断问题出在网络、响应格式，还是 API 返回无数据。
enum class ParseError {
  kNone = 0,
  kInvalidJson,   // 响应不是合法 JSON
  kApiCodeNotOk,  // 合法 JSON，但 API 业务码 != "200"
  kMissingData,   // 业务码正常，但期望的字段缺失/为空
};

const char* ParseErrorName(ParseError error);

struct ParseResult {
  bool ok = false;
  ParseError error = ParseError::kNone;
  std::string message;   // 可直接打印的诊断信息
  std::string api_code;  // API 返回的业务码（诊断用，未返回则为空）

  static ParseResult Success();
  static ParseResult Failure(ParseError error, std::string message,
                             std::string api_code = {});
};

// 以下解析函数的输入是**已解压的** JSON 文本（和风天气响应为 gzip）。
// 它们不触碰网络、不触碰 UI、不依赖 ESP-IDF，因此可在主机上直接单测。
//
// 解析语义（各端点的取值规则）：
//   * 业务码 "200" 校验**只对返回 code 字段的端点生效** ——
//     city/lookup、weather/now、weather/7d 有 code；
//     airquality/v1/current **没有 code 字段**（已用真实响应核实），
//     因此对它不做业务码校验，只依据 indexes 是否存在判定。
//   * 城市查询取 location[0]；
//   * 实时天气取 now；
//   * 7 日预报取 daily[]，最多 kDailyCount 条，多余条目丢弃、不足的补空；
//   * 空气质量取 indexes[0]，pm2.5 从 pollutants[] 中按 code/name 匹配。

ParseResult ParseCityLookup(const std::string& json, CityLookup& out);
ParseResult ParseWeatherNow(const std::string& json, WeatherNow& out);
ParseResult ParseDailyForecast(const std::string& json, DailyForecast& out);
ParseResult ParseAirQualityNow(const std::string& json, AirQualityNow& out);

}  // namespace weather
