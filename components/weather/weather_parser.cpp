#include "weather/weather_parser.h"

#include <cstdio>
#include <cstring>
#include <memory>

#include "cJSON.h"

namespace weather {
namespace {

// cJSON 的 RAII 包装，避免任何早退路径泄漏
struct CJsonDeleter {
  void operator()(cJSON* node) const {
    if (node != nullptr) {
      cJSON_Delete(node);
    }
  }
};
using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;

// 取对象下的标量字段并转成字符串。
//
// 为什么不能只用 cJSON_IsString：和风天气的部分字段是**数字**而非字符串
// （例如 indexes[].aqi、pollutants[].concentration.value）。cJSON 不会自动
// 把数字转成字符串，因此这里必须显式处理 number/bool，否则这些字段会静默
// 变成空串。
std::string GetScalar(const cJSON* object, const char* key) {
  if (object == nullptr) {
    return {};
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (item == nullptr) {
    return {};
  }
  if (cJSON_IsString(item) && item->valuestring != nullptr) {
    return std::string(item->valuestring);
  }
  if (cJSON_IsNumber(item)) {
    char buf[40];
    const double value = item->valuedouble;
    const long long truncated = static_cast<long long>(value);
    if (value == static_cast<double>(truncated)) {
      std::snprintf(buf, sizeof(buf), "%lld", truncated);
    } else {
      std::snprintf(buf, sizeof(buf), "%g", value);
    }
    return std::string(buf);
  }
  if (cJSON_IsBool(item)) {
    return cJSON_IsTrue(item) ? "true" : "false";
  }
  return {};
}

// 别名：语义上表示"取字符串"，但实际接受任何标量（见上）。
std::string GetString(const cJSON* object, const char* key) {
  return GetScalar(object, key);
}

// 取对象的字符串字段，为空时回退到另一个字段。
// 用于 aqi / aqiDisplay 这种新老字段并存的情况。
std::string GetStringWithFallback(const cJSON* object, const char* primary,
                                  const char* fallback) {
  std::string value = GetString(object, primary);
  if (value.empty()) {
    value = GetString(object, fallback);
  }
  return value;
}

std::string GetApiCode(const cJSON* root) { return GetString(root, "code"); }

// 解析入口的公共部分：解析 JSON，并（按端点）校验业务码。
//
// require_api_code 必须**按端点**决定，不能一刀切：
//   * /geo/v2/city/lookup、/v7/weather/now、/v7/weather/7d 会返回 "code":"200"；
//   * /airquality/v1/current/... **根本没有 code 字段**（已用真实响应核实）。
// 对后者也强制要求 code=="200"，会把全部合法的空气质量响应判为失败。
ParseResult ParseRoot(const std::string& json, CJsonPtr& out_root,
                      bool require_api_code) {
  if (json.empty()) {
    return ParseResult::Failure(ParseError::kInvalidJson, "empty response");
  }

  cJSON* parsed = cJSON_ParseWithLength(json.c_str(), json.size());
  if (parsed == nullptr) {
    const char* err = cJSON_GetErrorPtr();
    std::string message = "JSON parse failed";
    if (err != nullptr) {
      message += std::string(" (at: ") + err + ")";
    }
    return ParseResult::Failure(ParseError::kInvalidJson, std::move(message));
  }
  out_root.reset(parsed);

  if (!require_api_code) {
    return ParseResult::Success();
  }

  const std::string api_code = GetApiCode(parsed);
  if (api_code != "200") {
    return ParseResult::Failure(
        ParseError::kApiCodeNotOk,
        "api code not ok: " +
            (api_code.empty() ? std::string("<missing>") : api_code),
        api_code);
  }
  return ParseResult::Success();
}

}  // namespace

const char* ParseErrorName(ParseError error) {
  switch (error) {
    case ParseError::kNone:
      return "none";
    case ParseError::kInvalidJson:
      return "invalid_json";
    case ParseError::kApiCodeNotOk:
      return "api_code_not_ok";
    case ParseError::kMissingData:
      return "missing_data";
  }
  return "unknown";
}

ParseResult ParseResult::Success() {
  ParseResult result;
  result.ok = true;
  return result;
}

ParseResult ParseResult::Failure(ParseError error, std::string message,
                                 std::string api_code) {
  ParseResult result;
  result.ok = false;
  result.error = error;
  result.message = std::move(message);
  result.api_code = std::move(api_code);
  return result;
}

ParseResult ParseCityLookup(const std::string& json, CityLookup& out) {
  CJsonPtr root;
  ParseResult result = ParseRoot(json, root, /*require_api_code=*/true);
  if (!result.ok) {
    return result;
  }

  const cJSON* locations =
      cJSON_GetObjectItemCaseSensitive(root.get(), "location");
  if (!cJSON_IsArray(locations) || cJSON_GetArraySize(locations) == 0) {
    return ParseResult::Failure(ParseError::kMissingData,
                                "city lookup returned no location");
  }

  const cJSON* first = cJSON_GetArrayItem(locations, 0);
  CityLookup parsed;
  parsed.id = GetString(first, "id");
  parsed.name = GetString(first, "name");
  parsed.adm2 = GetString(first, "adm2");
  parsed.adm1 = GetString(first, "adm1");
  // adm2 缺失时回退到 name（可能是区县名）。这个回退一定显示得出来：字集是
  // 省 ∪ 市 ∪ 区县 去重（1297 字，见 charset/city.txt），区县名已被覆盖。
  if (parsed.adm2.empty()) {
    parsed.adm2 = parsed.name;
  }
  parsed.lat = GetString(first, "lat");
  parsed.lon = GetString(first, "lon");

  if (parsed.id.empty()) {
    return ParseResult::Failure(ParseError::kMissingData,
                                "city lookup missing location id");
  }

  out = std::move(parsed);
  return ParseResult::Success();
}

ParseResult ParseWeatherNow(const std::string& json, WeatherNow& out) {
  CJsonPtr root;
  ParseResult result = ParseRoot(json, root, /*require_api_code=*/true);
  if (!result.ok) {
    return result;
  }

  const cJSON* now = cJSON_GetObjectItemCaseSensitive(root.get(), "now");
  if (!cJSON_IsObject(now)) {
    return ParseResult::Failure(ParseError::kMissingData,
                                "weather now missing 'now'");
  }

  WeatherNow parsed;
  parsed.temp = GetString(now, "temp");
  parsed.feelsLike = GetString(now, "feelsLike");
  parsed.icon = GetString(now, "icon");
  parsed.text = GetString(now, "text");
  parsed.windDir = GetString(now, "windDir");
  parsed.windScale = GetString(now, "windScale");
  parsed.humidity = GetString(now, "humidity");
  parsed.vis = GetString(now, "vis");

  // 天气描述与温度是实时天气页的必需字段：缺任何一个，页面都只能显示空洞。
  // 与其把半截数据交给页面，不如在这里明确判为"无数据"。
  if (parsed.text.empty() || parsed.temp.empty()) {
    return ParseResult::Failure(ParseError::kMissingData,
                                "weather now missing text/temp");
  }

  out = std::move(parsed);
  return ParseResult::Success();
}

ParseResult ParseDailyForecast(const std::string& json, DailyForecast& out) {
  CJsonPtr root;
  ParseResult result = ParseRoot(json, root, /*require_api_code=*/true);
  if (!result.ok) {
    return result;
  }

  const cJSON* daily = cJSON_GetObjectItemCaseSensitive(root.get(), "daily");
  if (!cJSON_IsArray(daily) || cJSON_GetArraySize(daily) == 0) {
    return ParseResult::Failure(ParseError::kMissingData,
                                "forecast returned no daily entries");
  }

  // 先把目标整块清空，确保不足 7 天时不会残留上一次的数据。
  // 用整块清空而不是在末尾逐条补空：以后给 WeatherDaily 加字段时不会漏。
  DailyForecast parsed{};

  const int available = cJSON_GetArraySize(daily);
  const int count = available < static_cast<int>(kDailyCount)
                        ? available
                        : static_cast<int>(kDailyCount);
  for (int i = 0; i < count; ++i) {
    const cJSON* day = cJSON_GetArrayItem(daily, i);
    WeatherDaily& entry = parsed[static_cast<std::size_t>(i)];
    entry.fxDate = GetString(day, "fxDate");
    entry.sunrise = GetString(day, "sunrise");
    entry.sunset = GetString(day, "sunset");
    entry.tempMax = GetString(day, "tempMax");
    entry.tempMin = GetString(day, "tempMin");
    entry.iconDay = GetString(day, "iconDay");
    entry.textDay = GetString(day, "textDay");
    entry.iconNight = GetString(day, "iconNight");
    entry.textNight = GetString(day, "textNight");
  }

  out = std::move(parsed);
  return ParseResult::Success();
}

ParseResult ParseAirQualityNow(const std::string& json, AirQualityNow& out) {
  CJsonPtr root;
  ParseResult result = ParseRoot(json, root, /*require_api_code=*/false);
  if (!result.ok) {
    return result;
  }

  const cJSON* indexes =
      cJSON_GetObjectItemCaseSensitive(root.get(), "indexes");
  if (!cJSON_IsArray(indexes) || cJSON_GetArraySize(indexes) == 0) {
    return ParseResult::Failure(ParseError::kMissingData,
                                "air quality missing indexes");
  }

  const cJSON* index = cJSON_GetArrayItem(indexes, 0);
  AirQualityNow parsed;
  // 优先 aqiDisplay，缺失时回退 aqi
  parsed.aqi = GetStringWithFallback(index, "aqiDisplay", "aqi");
  parsed.category = GetString(index, "category");

  const cJSON* pollutant =
      cJSON_GetObjectItemCaseSensitive(index, "primaryPollutant");
  parsed.primaryPollutant = GetString(pollutant, "name");

  // PM2.5：在 pollutants[] 中按 code 或中文名匹配
  const cJSON* pollutants =
      cJSON_GetObjectItemCaseSensitive(root.get(), "pollutants");
  if (cJSON_IsArray(pollutants)) {
    const int size = cJSON_GetArraySize(pollutants);
    for (int i = 0; i < size; ++i) {
      const cJSON* item = cJSON_GetArrayItem(pollutants, i);
      const std::string code = GetString(item, "code");
      const std::string name = GetString(item, "name");
      // 中文名匹配：请求统一带 lang=zh，服务端返回的是 "PM2.5"；
      // 保留中文匹配是为了万一语言参数失效时仍能识别（此处不参与显示）。
      if (code == "pm2p5" || name == "PM2.5" || name == "细颗粒物") {
        const cJSON* concentration =
            cJSON_GetObjectItemCaseSensitive(item, "concentration");
        parsed.pm2p5 = GetString(concentration, "value");
        break;
      }
    }
  }

  if (parsed.aqi.empty() && parsed.category.empty()) {
    return ParseResult::Failure(ParseError::kMissingData,
                                "air quality missing aqi/category");
  }

  out = std::move(parsed);
  return ParseResult::Success();
}

}  // namespace weather
