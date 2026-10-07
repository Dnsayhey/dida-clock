#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "esp_http_server.h"
#include "storage/device_config.h"

namespace portal {

// 表单校验通过后的落库回调。返回 true 表示已保存成功。
//
// 由 **httpd 任务**调用（不是 net 任务），实现方必须自己保证线程安全。
using SaveHandler = std::function<bool(const config::DeviceConfig&)>;

// HTTP 层要用到的门户状态。
//
// 刻意不传 CaptivePortal*：HTTP 层只需要"能读到可选网络、能落库、能请求重启"
// 这三件事。收窄成这组指针之后，它完全不必知道 AP / DHCP / 扫描 / DNS 的存在。
//
// 各指针指向的对象由 CaptivePortal 持有，其自身地址在门户生命周期内稳定
// （CaptivePortal 不可拷贝、不可移动）。
struct PortalHttpHooks {
  const std::vector<std::string>* wifi_options = nullptr;
  SaveHandler* on_save = nullptr;
  std::atomic<bool>* restart_requested = nullptr;
};

// 启动 HTTP 服务器并注册路由表（Routes()）与兜底重定向。
//
// 成功时把句柄写入 *server。注册路由失败时 *server 仍持有已启动的服务器，
// 由调用方停止 —— 这样错误恢复路径只在一处收尾。
//
// `hooks` 的地址会被 httpd 作为 user_ctx 长期保存，因此它必须比服务器活得久。
bool StartHttpServer(httpd_handle_t* server, const PortalHttpHooks* hooks);

// 停止 HTTP 服务器。*server 为空时安全空操作；停止后置 nullptr。
void StopHttpServer(httpd_handle_t* server);

}  // namespace portal
