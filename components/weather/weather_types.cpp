#include "weather/weather_types.h"

namespace weather {

const char* DataSourceName(DataSource source) {
  switch (source) {
    case DataSource::kCurrentConditions:
      return "current_conditions";
    case DataSource::kAirQuality:
      return "air_quality";
    case DataSource::kDailyForecast:
      return "daily_forecast";
    case DataSource::kCityLookup:
      return "city_lookup";
  }
  return "unknown";
}

const char* SyncStatusName(SyncStatus status) {
  switch (status) {
    case SyncStatus::kIdle:
      return "idle";
    case SyncStatus::kSyncing:
      return "syncing";
    case SyncStatus::kReady:
      return "ready";
    case SyncStatus::kFailed:
      return "failed";
  }
  return "unknown";
}

}  // namespace weather
