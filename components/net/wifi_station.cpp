#include "net/wifi_station.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <cstring>

#include "net/network_status.h"
#include "net/wifi_driver.h"

namespace net {
namespace {

constexpr char kTag[] = "wifi";

constexpr int kConnectedBit = BIT0;
constexpr int kFailedBit = BIT1;

EventGroupHandle_t g_events = nullptr;
esp_netif_t* g_sta_netif = nullptr;

// 当前连接配置。仅由 net 任务写，配合 PublishNetworkStatus 一起发布。
std::string g_ssid;

// 从 netif 读取 IP 字符串
std::string ReadIpString() {
  if (g_sta_netif == nullptr) {
    return {};
  }
  esp_netif_ip_info_t info = {};
  if (esp_netif_get_ip_info(g_sta_netif, &info) != ESP_OK) {
    return {};
  }
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), IPSTR, IP2STR(&info.ip));
  return std::string(buffer);
}

// 读取当前 RSSI；失败返回 0
int ReadRssi() {
  wifi_ap_record_t ap = {};
  if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
    return 0;
  }
  return ap.rssi;
}

void OnWifiEvent(void* /*arg*/, esp_event_base_t base, int32_t id,
                 void* event_data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    // 启动后不自动连接：由 Connect() 显式发起
    return;
  }

  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    // 发布"已断开"：所有读侧都通过 GetNetworkStatus() 拿拷贝
    NetworkStatus offline;
    offline.connected = false;
    PublishNetworkStatus(offline);

    if (g_events != nullptr) {
      xEventGroupSetBits(g_events, kFailedBit);
    }
    // ESP-IDF 6.0 部分断开原因码已改名。
    // 这里只记录原因码本身，避免依赖具体枚举名。
    const auto* event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
    if (event != nullptr) {
      ESP_LOGW(kTag, "Wi-Fi 断开，reason=%d", static_cast<int>(event->reason));
    }
    return;
  }

  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto* event = static_cast<ip_event_got_ip_t*>(event_data);
    if (event != nullptr) {
      ESP_LOGI(kTag, "拿到 IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
    NetworkStatus online;
    online.connected = true;
    online.ip = ReadIpString();
    online.ssid = g_ssid;
    online.rssi = ReadRssi();
    PublishNetworkStatus(online);

    if (g_events != nullptr) {
      xEventGroupSetBits(g_events, kConnectedBit);
    }
  }
}

}  // namespace

esp_err_t WifiStation::Begin() {
  if (initialized_) {
    return ESP_OK;
  }

  if (g_events == nullptr) {
    g_events = xEventGroupCreate();
    if (g_events == nullptr) {
      ESP_LOGE(kTag, "创建事件组失败");
      return ESP_ERR_NO_MEM;
    }
  }

  // 网络基础设施（含 WiFi 驱动）统一由 EnsureWifiDriverReady() 准备 ——
  // 它同样服务于强制门户那条路径，理由见 net/wifi_driver.h。
  const esp_err_t drv_err = EnsureWifiDriverReady();
  if (drv_err != ESP_OK) {
    return drv_err;
  }

  esp_err_t err = ESP_OK;
  if (g_sta_netif == nullptr) {
    g_sta_netif = esp_netif_create_default_wifi_sta();
    if (g_sta_netif == nullptr) {
      ESP_LOGE(kTag, "创建默认 STA netif 失败");
      return ESP_FAIL;
    }
  }

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &OnWifiEvent, nullptr, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &OnWifiEvent, nullptr, nullptr));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  err = esp_wifi_start();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "esp_wifi_start 失败: %s", esp_err_to_name(err));
    return err;
  }

  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

  initialized_ = true;
  ESP_LOGI(kTag, "Wi-Fi STA 已初始化");
  return ESP_OK;
}

bool WifiStation::Connect(const std::string& ssid, const std::string& password,
                          uint32_t timeout_ms) {
  if (!initialized_) {
    ESP_LOGE(kTag, "Connect 前必须先 Begin()");
    return false;
  }
  if (ssid.empty()) {
    ESP_LOGE(kTag, "SSID 为空");
    return false;
  }

  wifi_config_t wifi_config = {};
  // SSID / password 字段分别是 32 / 64 字节（含结尾 NUL）
  if (ssid.size() >= sizeof(wifi_config.sta.ssid) ||
      password.size() >= sizeof(wifi_config.sta.password)) {
    ESP_LOGE(kTag, "SSID 或密码过长");
    return false;
  }
  g_ssid = ssid;
  std::memcpy(wifi_config.sta.ssid, ssid.c_str(), ssid.size());
  std::memcpy(wifi_config.sta.password, password.c_str(), password.size());
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  xEventGroupClearBits(g_events, kConnectedBit | kFailedBit);

  ESP_LOGI(kTag, "连接 \"%s\" ...", ssid.c_str());
  esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_wifi_connect 失败: %s", esp_err_to_name(err));
    return false;
  }

  const EventBits_t bits =
      xEventGroupWaitBits(g_events, kConnectedBit | kFailedBit, pdFALSE,
                          pdFALSE, pdMS_TO_TICKS(timeout_ms));

  if ((bits & kConnectedBit) != 0) {
    ESP_LOGI(kTag, "连接成功");
    return true;
  }

  ESP_LOGW(kTag, "连接超时或失败（bits=0x%x）", static_cast<unsigned>(bits));
  esp_wifi_disconnect();
  return false;
}

void WifiStation::Disconnect() {
  if (initialized_) {
    esp_wifi_disconnect();
    NetworkStatus offline;
    offline.connected = false;
    PublishNetworkStatus(offline);
  }
}

// 以下三个访问器统一从 NetworkStatus 读取（线程安全），
// 不再直接触碰由 net 任务写的裸全局 —— 那是跨任务无保护访问。
bool WifiStation::IsConnected() const { return GetNetworkStatus().connected; }

std::string WifiStation::IpAddress() const { return GetNetworkStatus().ip; }

int WifiStation::Rssi() const { return GetNetworkStatus().rssi; }

bool IsNetworkUp() { return GetNetworkStatus().connected; }

}  // namespace net
