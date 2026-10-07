#include "input/button_event.h"

namespace input {

const char* ButtonEventName(ButtonEvent event) {
  switch (event) {
    case ButtonEvent::kNone:
      return "none";
    case ButtonEvent::kClick:
      return "click";
    case ButtonEvent::kDoubleClick:
      return "double_click";
    case ButtonEvent::kLongPress:
      return "long_press";
  }
  return "unknown";
}

}  // namespace input
