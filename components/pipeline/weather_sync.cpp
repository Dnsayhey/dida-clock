#include "pipeline/weather_sync.h"

#include <esp_log.h>
#include <esp_timer.h>

#include <cstdint>
#include <cstdio>

#include "decompress/decompress.h"
#include "net/http_client.h"
#include "weather/weather_api.h"
#include "weather/weather_parser.h"

namespace pipeline {
namespace {

constexpr char kTag[] = "weather_sync";

// 单次 HTTP 超时。天气响应很小，30 秒足够；太长会拖住 net 任务。
constexpr uint32_t kHttpTimeoutMs = 30000;

// 单调毫秒时基（自启动起）。WeatherStore 刻意要求时间戳由调用方传入，
// 以免把平台时间依赖带进 store（那样就没法在主机上单测了）。
uint64_t NowMs() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

// 把 ParseResult 转成面向用户/日志的失败原因。
std::string DescribeParseFailure(const weather::ParseResult& result) {
  std::string reason = weather::ParseErrorName(result.error);
  if (!result.message.empty()) {
    reason += ": ";
    reason += result.message;
  }
  return reason;
}

}  // namespace

WeatherSync::WeatherSync(store::WeatherStore& store,
                         WeatherApiConfig api_config)
    : store_(store), api_config_(std::move(api_config)) {}

bool WeatherSync::FetchJson(const std::string& url, std::string& json_out,
                            std::string& error) {
  const net::HttpResponse response = net::HttpGet(url, kHttpTimeoutMs);
  if (!response.ok) {
    // 网络层失败（未连接、超时、DNS 失败……）
    error = "network: " + response.error;
    return false;
  }
  if (!response.IsHttpOk()) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "http_status: %d", response.status);
    error = buffer;
    return false;
  }

  std::string decompress_error;
  if (!decompress::Inflate(response.body, json_out, decompress_error)) {
    error = "decompress: " + decompress_error;
    return false;
  }
  return true;
}

bool WeatherSync::EnsureLocation(const config::DeviceConfig& device_config) {
  // 配置里已有完整信息，直接用
  if (config::IsWeatherQueryReady(device_config)) {
    location_id_ = device_config.location_id;
    latitude_ = device_config.location_lat;
    longitude_ = device_config.location_lon;
    return true;
  }

  if (!api_config_.IsConfigured()) {
    store_.MarkFailed(weather::DataSource::kCityLookup,
                      "Weather API not configured", NowMs());
    ESP_LOGW(kTag, "weather API not configured, skipping lookup");
    return false;
  }
  if (device_config.location.empty()) {
    store_.MarkFailed(weather::DataSource::kCityLookup, "Location name not set",
                      NowMs());
    return false;
  }

  store_.MarkSyncing(weather::DataSource::kCityLookup);

  const std::string url =
      weather::BuildCityLookupUrl(api_config_.base_url, api_config_.api_key,
                                  device_config.location, device_config.adm);

  std::string json;
  std::string error;
  if (!FetchJson(url, json, error)) {
    store_.MarkFailed(weather::DataSource::kCityLookup, error, NowMs());
    ESP_LOGW(kTag, "city lookup failed: %s", error.c_str());
    return false;
  }

  weather::CityLookup city;
  const weather::ParseResult result = weather::ParseCityLookup(json, city);
  if (!result.ok) {
    const std::string reason = DescribeParseFailure(result);
    store_.MarkFailed(weather::DataSource::kCityLookup, reason, NowMs());
    ESP_LOGW(kTag, "city lookup parse failed: %s", reason.c_str());
    return false;
  }

  location_id_ = city.id;
  latitude_ = city.lat;
  longitude_ = city.lon;

  store_.SetCityLookup(city);
  store_.MarkReady(weather::DataSource::kCityLookup, NowMs());
  ESP_LOGI(kTag, "located: %s (id=%s, %s,%s)", city.name.c_str(),
           city.id.c_str(), city.lat.c_str(), city.lon.c_str());
  return true;
}

void WeatherSync::SyncCurrentConditions(const std::string& location_id) {
  if (!api_config_.IsConfigured()) {
    store_.MarkFailed(weather::DataSource::kCurrentConditions,
                      "Weather API not configured", 0);
    return;
  }
  if (location_id.empty()) {
    store_.MarkFailed(weather::DataSource::kCurrentConditions,
                      "Missing location id", 0);
    return;
  }

  store_.MarkSyncing(weather::DataSource::kCurrentConditions);

  const std::string url = weather::BuildWeatherNowUrl(
      api_config_.base_url, api_config_.api_key, location_id);

  std::string json;
  std::string error;
  if (!FetchJson(url, json, error)) {
    store_.MarkFailed(weather::DataSource::kCurrentConditions, error, NowMs());
    ESP_LOGW(kTag, "weather fetch failed: %s", error.c_str());
    return;
  }

  weather::WeatherNow now;
  const weather::ParseResult result = weather::ParseWeatherNow(json, now);
  if (!result.ok) {
    const std::string reason = DescribeParseFailure(result);
    store_.MarkFailed(weather::DataSource::kCurrentConditions, reason, NowMs());
    ESP_LOGW(kTag, "weather parse failed: %s", reason.c_str());
    return;
  }

  store_.SetCurrentConditions(now);
  store_.MarkReady(weather::DataSource::kCurrentConditions, NowMs());
  ESP_LOGI(kTag, "weather: %s %sC  hum %s%%", now.text.c_str(),
           now.temp.c_str(), now.humidity.c_str());
}

void WeatherSync::SyncDailyForecast(const std::string& location_id) {
  if (!api_config_.IsConfigured()) {
    store_.MarkFailed(weather::DataSource::kDailyForecast,
                      "Weather API not configured", NowMs());
    return;
  }
  if (location_id.empty()) {
    store_.MarkFailed(weather::DataSource::kDailyForecast,
                      "Missing location id", NowMs());
    return;
  }

  store_.MarkSyncing(weather::DataSource::kDailyForecast);

  const std::string url = weather::BuildDailyForecastUrl(
      api_config_.base_url, api_config_.api_key, location_id);

  std::string json;
  std::string error;
  if (!FetchJson(url, json, error)) {
    store_.MarkFailed(weather::DataSource::kDailyForecast, error, NowMs());
    ESP_LOGW(kTag, "forecast fetch failed: %s", error.c_str());
    return;
  }

  weather::DailyForecast daily;
  const weather::ParseResult result = weather::ParseDailyForecast(json, daily);
  if (!result.ok) {
    const std::string reason = DescribeParseFailure(result);
    store_.MarkFailed(weather::DataSource::kDailyForecast, reason, NowMs());
    ESP_LOGW(kTag, "forecast parse failed: %s", reason.c_str());
    return;
  }

  store_.SetDailyForecast(daily);
  store_.MarkReady(weather::DataSource::kDailyForecast, NowMs());
  ESP_LOGI(kTag, "forecast: %s %s %s/%sC", daily[0].fxDate.c_str(),
           daily[0].textDay.c_str(), daily[0].tempMin.c_str(),
           daily[0].tempMax.c_str());
}

void WeatherSync::SyncAirQuality(const std::string& latitude,
                                 const std::string& longitude) {
  if (!api_config_.IsConfigured()) {
    store_.MarkFailed(weather::DataSource::kAirQuality,
                      "Weather API not configured", NowMs());
    return;
  }
  if (latitude.empty() || longitude.empty()) {
    store_.MarkFailed(weather::DataSource::kAirQuality, "Missing coordinates",
                      NowMs());
    return;
  }

  store_.MarkSyncing(weather::DataSource::kAirQuality);

  const std::string url = weather::BuildAirQualityUrl(
      api_config_.base_url, api_config_.api_key, latitude, longitude);

  std::string json;
  std::string error;
  if (!FetchJson(url, json, error)) {
    store_.MarkFailed(weather::DataSource::kAirQuality, error, NowMs());
    ESP_LOGW(kTag, "air quality fetch failed: %s", error.c_str());
    return;
  }

  weather::AirQualityNow air;
  const weather::ParseResult result = weather::ParseAirQualityNow(json, air);
  if (!result.ok) {
    // 注意：空气质量端点没有 code 字段，ParseAirQualityNow 不会做业务码校验
    const std::string reason = DescribeParseFailure(result);
    store_.MarkFailed(weather::DataSource::kAirQuality, reason, NowMs());
    ESP_LOGW(kTag, "air quality parse failed: %s", reason.c_str());
    return;
  }

  store_.SetAirQuality(air);
  store_.MarkReady(weather::DataSource::kAirQuality, NowMs());
  ESP_LOGI(kTag, "air: AQI %s %s, PM2.5 %s", air.aqi.c_str(),
           air.category.c_str(), air.pm2p5.c_str());
}

}  // namespace pipeline
