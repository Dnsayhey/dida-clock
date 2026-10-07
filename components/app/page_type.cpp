#include "app/app_controller.h"
#include "app/app_events.h"

namespace app {

const char* PageTypeName(PageType page) {
  switch (page) {
    case PageType::kInit:
      return "init";
    case PageType::kNetworkSetup:
      return "network_setup";
    case PageType::kRealTimeWeather:
      return "real_time_weather";
    case PageType::kFutureWeather:
      return "future_weather";
    case PageType::kThemeSettings:
      return "theme_settings";
    case PageType::kFactoryReset:
      return "factory_reset";
    case PageType::kCount:
      break;
  }
  return "unknown";
}

}  // namespace app
