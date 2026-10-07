#include "app/app_command_executor.h"

namespace app {

void AppCommandExecutor::Execute(const AppCommand& command,
                                 PageHost& host) const {
  switch (command.type) {
    case AppCommandType::kSwitchPage:
      host.SwitchTo(command.target_page);
      break;

    case AppCommandType::kDispatchPageClick:
      host.DispatchClickToCurrent();
      break;

    case AppCommandType::kDispatchPageLongPress:
      host.DispatchLongPressToCurrent();
      break;

    case AppCommandType::kNone:
      break;
  }
}

}  // namespace app
