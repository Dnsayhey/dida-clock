#include "weather/weather_api.h"

#include <cctype>

namespace weather {
namespace {

// 所有请求都要求**中文**文本（界面是中文，见各 URL 构造函数处的说明）。
constexpr char kLangQuery[] = "&lang=zh";

}  // namespace
namespace {

constexpr char kHexDigits[] = "0123456789ABCDEF";

// 拼接 base + path + "?key=<key>" + query
std::string ApiUrl(const std::string& base, const std::string& path,
                   const std::string& key, const std::string& query) {
  return base + path + "?key=" + UrlEncode(key) + query;
}

}  // namespace

std::string UrlEncode(const std::string& input) {
  std::string encoded;
  encoded.reserve(input.size());

  for (const unsigned char raw : input) {
    const char c = static_cast<char>(raw);
    if (c == ' ') {
      encoded += '+';
    } else if (std::isalnum(raw) != 0 || c == '-' || c == '_' || c == '.' ||
               c == '~') {
      encoded += c;
    } else {
      encoded += '%';
      encoded += kHexDigits[(raw >> 4) & 0x0F];
      encoded += kHexDigits[raw & 0x0F];
    }
  }
  return encoded;
}

std::string BuildCityLookupUrl(const std::string& base, const std::string& key,
                               const std::string& city,
                               const std::string& adm) {
  std::string query = "&location=" + UrlEncode(city);
  if (!adm.empty()) {
    query += "&adm=" + UrlEncode(adm);
  }
  // 界面是中文：让服务端直接返回中文文本（天气描述、风向、城市名），
  // 省掉本地再翻译一层。
  query += kLangQuery;
  return ApiUrl(base, "/geo/v2/city/lookup", key, query);
}

std::string BuildWeatherNowUrl(const std::string& base, const std::string& key,
                               const std::string& location_id) {
  return ApiUrl(base, "/v7/weather/now", key,
                "&location=" + UrlEncode(location_id) + kLangQuery);
}

std::string BuildDailyForecastUrl(const std::string& base,
                                  const std::string& key,
                                  const std::string& location_id) {
  return ApiUrl(base, "/v7/weather/7d", key,
                "&location=" + UrlEncode(location_id) + kLangQuery);
}

std::string BuildAirQualityUrl(const std::string& base, const std::string& key,
                               const std::string& latitude,
                               const std::string& longitude) {
  return base + "/airquality/v1/current/" + UrlEncode(latitude) + "/" +
         UrlEncode(longitude) + "?key=" + UrlEncode(key) + kLangQuery;
}

}  // namespace weather
