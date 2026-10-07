#include "net/wifi_driver.h"

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include "esp_log.h"

namespace net {
namespace {

constexpr char kTag[] = "wifi_driver";

bool g_driver_ready = false;

}  // namespace

esp_err_t EnsureWifiDriverReady() {
  if (g_driver_ready) {
    return ESP_OK;
  }

  // ---- 网络栈（lwIP）----
  // 必须早于 esp_wifi_init 与任何 socket 操作：否则 lwIP 的 tcpip 线程尚未
  // 建立，esp_sntp_init() 会断言 "Invalid mbox" 并重启。
  const esp_err_t netif_err = esp_netif_init();
  if (netif_err != ESP_OK && netif_err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "esp_netif_init 失败: %s", esp_err_to_name(netif_err));
    return netif_err;
  }

  // ---- 默认事件循环（WiFi/IP 事件都挂在这上面）----
  const esp_err_t loop_err = esp_event_loop_create_default();
  if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "创建默认事件循环失败: %s", esp_err_to_name(loop_err));
    return loop_err;
  }

  // ---- WiFi 驱动本体 ----
  const wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
  const esp_err_t wifi_err = esp_wifi_init(&wifi_cfg);
  // 重复 init 返回 ESP_ERR_INVALID_STATE（IDF 6.x 行为）：它表示"已经就绪"，
  // 不是失败。
  if (wifi_err != ESP_OK && wifi_err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "esp_wifi_init 失败: %s", esp_err_to_name(wifi_err));
    return wifi_err;
  }

  g_driver_ready = true;
  return ESP_OK;
}

}  // namespace net
