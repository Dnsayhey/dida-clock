#include "portal/captive_portal.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "portal/portal_http.h"
#include "portal/portal_wifi_scan.h"
#include "storage/device_config.h"

namespace portal {
namespace {

constexpr char kTag[] = "portal";
constexpr uint16_t kDnsPort = 53;

}  // namespace

CaptivePortal::~CaptivePortal() { Stop(); }

// ---------------- 启动 ----------------

bool CaptivePortal::Begin(SaveHandler on_save) {
  if (running_) {
    return true;
  }
  on_save_ = std::move(on_save);

  // 先扫描再起 AP：AP 已工作、可能有客户端连着时切到 STA 扫描会打断连接。
  // 扫描结束后 WiFi 处于停止状态，正好交给 StartSoftAp() 从干净状态启动。
  wifi_options_ = ScanWifiOptions();

  if (!StartSoftAp()) {
    return false;
  }
  if (!ConfigureDhcpDns()) {
    ESP_LOGW(kTag, "DHCP 的 DNS 下发配置失败，部分手机可能不弹门户");
  }

  // HTTP 层只需要这三样状态，收窄成指针传过去，它便不必知道 AP/DNS 的存在。
  // http_hooks_ 是成员，地址在门户生命周期内稳定。
  http_hooks_.wifi_options = &wifi_options_;
  http_hooks_.on_save = &on_save_;
  http_hooks_.restart_requested = &restart_requested_;
  if (!StartHttpServer(&server_, &http_hooks_)) {
    Stop();
    return false;
  }

  // 通配 DNS：把任意域名解析到本机 IP
  ip4_addr_t ap_ip = {};
  if (!inet_aton(ip_.c_str(), &ap_ip)) {
    ESP_LOGE(kTag, "解析 SoftAP IP 失败: %s", ip_.c_str());
    Stop();
    return false;
  }
  if (!dns_.Begin(ap_ip.addr, kDnsPort)) {
    ESP_LOGW(kTag, "通配 DNS 启动失败，强制门户可能无法自动弹出");
  }

  running_ = true;
  ESP_LOGI(kTag, "强制门户已启动: SSID=%s IP=%s 可选网络=%u", ssid_.c_str(),
           ip_.c_str(), static_cast<unsigned>(wifi_options_.size()));
  return true;
}

bool CaptivePortal::StartSoftAp() {
  // SSID 后缀取芯片 MAC 低 16 位：同一固件的多台设备热点名不会撞车，
  // 且重启后保持不变（用户手机里保存的热点不会失效）
  uint8_t mac[6] = {};
  esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "读取 MAC 失败: %s", esp_err_to_name(err));
    return false;
  }
  const uint16_t suffix =
      static_cast<uint16_t>((static_cast<uint16_t>(mac[4]) << 8) | mac[5]);
  ssid_ = BuildPortalSsid(suffix);

  ap_netif_ = esp_netif_create_default_wifi_ap();
  if (ap_netif_ == nullptr) {
    ESP_LOGE(kTag, "创建默认 AP netif 失败");
    return false;
  }

  wifi_config_t ap_config = {};
  std::strncpy(reinterpret_cast<char*>(ap_config.ap.ssid), ssid_.c_str(),
               sizeof(ap_config.ap.ssid) - 1);
  ap_config.ap.ssid_len = static_cast<uint8_t>(ssid_.size());
  std::strncpy(reinterpret_cast<char*>(ap_config.ap.password),
               kPortalApPassword, sizeof(ap_config.ap.password) - 1);
  ap_config.ap.max_connection = 4;
  ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ap_config.ap.channel = 1;

  err = esp_wifi_set_mode(WIFI_MODE_AP);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "设置 AP 模式失败: %s", esp_err_to_name(err));
    return false;
  }

  esp_wifi_set_config(WIFI_IF_AP, &ap_config);
  err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "启动 SoftAP 失败: %s", esp_err_to_name(err));
    return false;
  }

  esp_netif_ip_info_t ip_info = {};
  err = esp_netif_get_ip_info(ap_netif_, &ip_info);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "读取 SoftAP IP 失败: %s", esp_err_to_name(err));
    return false;
  }
  char buffer[16];
  esp_ip4addr_ntoa(&ip_info.ip, buffer, sizeof(buffer));
  ip_ = buffer;
  return true;
}

// 显式配置 DHCP 下发的 DNS 指向本机。
//
// esp_netif_dhcps_option() 的域名服务器选项语义反直觉：
//   * esp_netif_dhcps_option(SET, ESP_NETIF_DOMAIN_NAME_SERVER, &v, 1)
//     里的 v 只是一个 **bool 开关**（是否下发 option 6），**不是 DNS 地址**；
//   * DNS 地址来自 esp_netif_set_dns_info()，它内部走
//     dhcps_dns_setserver_by_type() 写进 dhcps->dns_server[]；
//   * 而且 dhcps_option 的 SET 在 **DHCP 服务器已启动时直接返回
//     ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED**。
// 默认的 esp_netif_create_default_wifi_ap() 已经把 DHCP 服务器起起来了，
// 所以必须 stop -> 配置 -> start，否则这次配置会静默失败。
bool CaptivePortal::ConfigureDhcpDns() {
  if (ap_netif_ == nullptr) {
    return false;
  }

  esp_err_t err = esp_netif_dhcps_stop(ap_netif_);
  if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
    ESP_LOGE(kTag, "停止 DHCP 服务器失败: %s", esp_err_to_name(err));
    return false;
  }

  // 1) 设置要下发的 DNS 地址为本机
  esp_netif_ip_info_t ip_info = {};
  esp_netif_get_ip_info(ap_netif_, &ip_info);

  esp_netif_dns_info_t dns_info = {};
  dns_info.ip.type = ESP_IPADDR_TYPE_V4;
  dns_info.ip.u_addr.ip4.addr = ip_info.ip.addr;
  err = esp_netif_set_dns_info(ap_netif_, ESP_NETIF_DNS_MAIN, &dns_info);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "设置 DHCP DNS 地址失败: %s", esp_err_to_name(err));
    esp_netif_dhcps_start(ap_netif_);
    return false;
  }

  // 2) 打开"下发 option 6"的开关（注意是 1 字节的 bool）
  uint8_t offer_dns = 1;
  err = esp_netif_dhcps_option(ap_netif_, ESP_NETIF_OP_SET,
                               ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns,
                               sizeof(offer_dns));
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "开启 DHCP 下发 DNS 失败: %s", esp_err_to_name(err));
    esp_netif_dhcps_start(ap_netif_);
    return false;
  }

  err = esp_netif_dhcps_start(ap_netif_);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "启动 DHCP 服务器失败: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(kTag, "DHCP 已配置为下发 DNS 指向本机");
  return true;
}

// ---------------- 停止 ----------------

void CaptivePortal::Stop() {
  dns_.Stop();
  StopHttpServer(&server_);
  if (ap_netif_ != nullptr) {
    esp_netif_destroy_default_wifi(ap_netif_);
    ap_netif_ = nullptr;
  }
  if (running_) {
    esp_wifi_stop();
  }
  running_ = false;
  ESP_LOGI(kTag, "强制门户已停止");
}

void CaptivePortal::Poll() {
  if (!running_) {
    return;
  }

  // 处理待发的重启请求（在 net 任务里做，保证 HTTP 响应已发出）
  if (restart_requested_) {
    ESP_LOGI(kTag, "执行配网后重启");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
  }

  // 排空 DNS 查询
  if (!dns_.Running()) {
    return;
  }
  for (int i = 0; i < 8; ++i) {
    // DnsHijack 内部负责收发；这里只是驱动节奏
    if (!dns_.PollOnce()) {
      break;
    }
  }
}

}  // namespace portal
