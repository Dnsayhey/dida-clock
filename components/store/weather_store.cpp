#include "store/weather_store.h"

#include <utility>

namespace store {
namespace {

// 一次快照拷贝的全部字段。集中在这里，避免将来加字段时漏掉某一处。
void CopyInto(WeatherSnapshot& dst, const WeatherSnapshot& src) { dst = src; }

}  // namespace

weather::SyncState& WeatherStore::StateLocked(weather::DataSource source) {
  switch (source) {
    case weather::DataSource::kCurrentConditions:
      return data_.now_state;
    case weather::DataSource::kAirQuality:
      return data_.air_quality_state;
    case weather::DataSource::kDailyForecast:
      return data_.daily_state;
    case weather::DataSource::kCityLookup:
      return data_.city_state;
  }
  // 所有枚举值都已覆盖；下一行仅为满足编译器的返回值要求。
  return data_.now_state;
}

void WeatherStore::BumpSequenceLocked() { ++data_.sequence; }

void WeatherStore::SetCurrentConditions(const weather::WeatherNow& now) {
  std::lock_guard<std::mutex> lock(mutex_);
  data_.now = now;
  BumpSequenceLocked();
}

void WeatherStore::SetAirQuality(const weather::AirQualityNow& air_quality) {
  std::lock_guard<std::mutex> lock(mutex_);
  data_.air_quality = air_quality;
  BumpSequenceLocked();
}

void WeatherStore::SetDailyForecast(const weather::DailyForecast& daily) {
  std::lock_guard<std::mutex> lock(mutex_);
  data_.daily = daily;
  BumpSequenceLocked();
}

void WeatherStore::SetCityLookup(const weather::CityLookup& city) {
  std::lock_guard<std::mutex> lock(mutex_);
  data_.city = city;
  BumpSequenceLocked();
}

void WeatherStore::MarkSyncing(weather::DataSource source) {
  std::lock_guard<std::mutex> lock(mutex_);
  weather::SyncState& state = StateLocked(source);
  state.status = weather::SyncStatus::kSyncing;
  state.message.clear();
  BumpSequenceLocked();
}

void WeatherStore::MarkReady(weather::DataSource source, uint64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  weather::SyncState& state = StateLocked(source);
  state.status = weather::SyncStatus::kReady;
  state.message.clear();
  state.updated_at_ms = now_ms;
  BumpSequenceLocked();
}

void WeatherStore::MarkFailed(weather::DataSource source, std::string message,
                              uint64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  weather::SyncState& state = StateLocked(source);
  state.status = weather::SyncStatus::kFailed;
  state.message = std::move(message);
  state.updated_at_ms = now_ms;
  BumpSequenceLocked();
}

WeatherSnapshot WeatherStore::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  WeatherSnapshot copy;
  CopyInto(copy, data_);
  return copy;  // 在锁内完成拷贝，出锁后调用方独占
}

uint32_t WeatherStore::Sequence() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return data_.sequence;
}

}  // namespace store
