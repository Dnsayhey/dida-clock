#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "esp_http_server.h"
#include "esp_netif.h"
#include "portal/dns_hijack.h"
#include "portal/portal_http.h"
#include "portal/portal_page.h"
#include "storage/device_config.h"

namespace portal {

// 强制门户：SoftAP + DHCP/DNS + HTTP 服务器 + 通配 DNS。
//
// 本类只负责**生命周期**：拉起 AP、配好 DHCP/DNS、启停 HTTP 服务、驱动 DNS。
// HTTP 的具体处理在 portal_http.{h,cpp}，WiFi 扫描在 portal_wifi_scan.{h,cpp}。
//
// **并发约定**：
//   * Begin()/Stop() 只能在 net 任务调用（含阻塞的 WiFi 扫描与网络栈操作）
//   * HTTP 请求由 esp_http_server 自己的任务处理，因此 SaveHandler 会从
//     **httpd 任务**被调用 —— 实现方必须自己保证线程安全
//   * Poll() 由调度器的周期任务调用（同样在 net 任务里）
class CaptivePortal {
 public:
  ~CaptivePortal();

  CaptivePortal() = default;
  CaptivePortal(const CaptivePortal&) = delete;
  CaptivePortal& operator=(const CaptivePortal&) = delete;

  bool Begin(SaveHandler on_save);
  void Stop();

  bool Running() const { return running_; }
  const std::string& Ssid() const { return ssid_; }
  const std::string& Ip() const { return ip_; }
  const std::vector<std::string>& WifiOptions() const { return wifi_options_; }

  // 处理 DNS 查询。未运行时立即返回。
  void Poll();

 private:
  bool StartSoftAp();
  bool ConfigureDhcpDns();

  // HTTP 层回读门户状态的接缝。作为成员持有以保证地址稳定 ——
  // httpd 会把它的地址存进 user_ctx 并长期使用。
  PortalHttpHooks http_hooks_;

  httpd_handle_t server_ = nullptr;
  esp_netif_t* ap_netif_ = nullptr;
  DnsHijack dns_;
  SaveHandler on_save_;

  std::string ssid_;
  std::string ip_;
  std::vector<std::string> wifi_options_;
  bool running_ = false;

  // 由 httpd 任务置位，由 net 任务在 Poll() 里执行重启。
  // 直接在 httpd 任务里 esp_restart() 会让确认页面来不及发出去。
  std::atomic<bool> restart_requested_{false};
};

}  // namespace portal
