#include "app/app_events.h"

namespace app {

const char* AppCommandTypeName(AppCommandType type) {
  switch (type) {
    case AppCommandType::kNone:
      return "none";
    case AppCommandType::kSwitchPage:
      return "switch_page";
    case AppCommandType::kDispatchPageClick:
      return "dispatch_page_click";
    case AppCommandType::kDispatchPageLongPress:
      return "dispatch_page_long_press";
  }
  return "unknown";
}

}  // namespace app
