#include "portal/portal_wifi_scan.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>

namespace portal {
namespace {

constexpr char kTag[] = "portal";

// 下拉列表最多展示这么多网络：再多手机上也翻不动。
constexpr std::size_t kMaxWifiOptions = 12;

}  // namespace

std::vector<std::string> ScanWifiOptions() {
  std::vector<std::string> options;

  // 扫描必须在 WiFi **已启动**的状态下进行。本函数在启动 AP 之前被调用，
  // 此时 WiFi 还是停止的，直接 esp_wifi_scan_start() 只会得到
  // ESP_ERR_WIFI_NOT_STARTED、列表为空。
  //
  // 调用方（CaptivePortal::Begin）随后会从停止状态去启动 AP，所以这里扫完
  // 必须 esp_wifi_stop() 收尾。
  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "切换 STA 模式失败，跳过扫描: %s", esp_err_to_name(err));
    return options;
  }
  err = esp_wifi_start();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "为扫描启动 WiFi 失败: %s", esp_err_to_name(err));
    return options;
  }

  wifi_scan_config_t scan_config = {};
  scan_config.show_hidden = false;
  err = esp_wifi_scan_start(&scan_config, /*block=*/true);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "WiFi 扫描失败: %s", esp_err_to_name(err));
    esp_wifi_scan_stop();
    esp_wifi_stop();
    return options;
  }

  uint16_t count = 0;
  esp_wifi_scan_get_ap_num(&count);
  if (count == 0) {
    ESP_LOGI(kTag, "扫描未发现网络");
    esp_wifi_scan_stop();
    esp_wifi_stop();
    return options;
  }

  std::vector<wifi_ap_record_t> records(count);
  esp_wifi_scan_get_ap_records(&count, records.data());

  for (uint16_t i = 0; i < count; ++i) {
    const char* ssid = reinterpret_cast<const char*>(records[i].ssid);
    if (ssid[0] == '\0') {
      continue;
    }
    const std::string candidate(ssid);
    bool duplicate = false;
    for (const std::string& existing : options) {
      if (existing == candidate) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      options.push_back(candidate);
      if (options.size() >= kMaxWifiOptions) {
        break;
      }
    }
  }

  esp_wifi_scan_stop();
  esp_wifi_stop();
  ESP_LOGI(kTag, "扫描到 %u 个可选网络", static_cast<unsigned>(options.size()));
  return options;
}

}  // namespace portal
