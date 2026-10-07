#pragma once

#include <string>

namespace weather {

// URL 编码（application/x-www-form-urlencoded）。规则：
//   * 空格 -> '+'
//   * 字母/数字以及 - _ . ~ 原样保留
//   * 其余字节 -> %XX（大写十六进制）
// 可在主机上直接单测。
std::string UrlEncode(const std::string& input);

// 和风天气的 4 个端点。返回完整的请求 URL。
// base 来自本地忽略配置（如 "https://xxx.re.qweatherapi.com"）。
// key 为空或 base 为空时，调用方应先判定为"天气 API 未配置"。

std::string BuildCityLookupUrl(const std::string& base, const std::string& key,
                               const std::string& city,
                               const std::string& adm = {});

std::string BuildWeatherNowUrl(const std::string& base, const std::string& key,
                               const std::string& location_id);

std::string BuildDailyForecastUrl(const std::string& base,
                                  const std::string& key,
                                  const std::string& location_id);

std::string BuildAirQualityUrl(const std::string& base, const std::string& key,
                               const std::string& latitude,
                               const std::string& longitude);

}  // namespace weather
