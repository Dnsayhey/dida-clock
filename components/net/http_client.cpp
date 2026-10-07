#include "net/http_client.h"

#include <esp_http_client.h>
#include <esp_log.h>

#include <cstdio>
#include <cstring>

#include "esp_crt_bundle.h"
#include "net/wifi_station.h"

namespace net {
namespace {

// 日志脱敏：截掉 URL 里的 key/token 查询参数 —— 和风天气把凭据放在查询串里，
// 直接打印完整 URL 会把 API Key 写进串口日志（CONVENTIONS §10 禁止）。
std::string RedactUrl(const std::string& url) {
  std::string out = url;
  for (const char* name : {"key=", "token=", "apikey="}) {
    std::size_t pos = 0;
    while ((pos = out.find(name, pos)) != std::string::npos) {
      const std::size_t value_start = pos + std::strlen(name);
      std::size_t value_end = out.find('&', value_start);
      if (value_end == std::string::npos) {
        value_end = out.size();
      }
      out.replace(value_start, value_end - value_start, "***");
      pos = value_start + 3;
    }
  }
  return out;
}

constexpr char kTag[] = "http";

// 天气响应解压后也只有几 KB，但给足余量避免大响应被截断。
// 超过上限会明确报错，而不是静默给出半截 JSON。
constexpr std::size_t kMaxBodyBytes = 32 * 1024;

struct BodyCollector {
  std::string body;
  bool overflow = false;
};

esp_err_t OnHttpEvent(esp_http_client_event_t* event) {
  auto* collector = static_cast<BodyCollector*>(event->user_data);
  if (collector == nullptr) {
    return ESP_OK;
  }

  if (event->event_id == HTTP_EVENT_ON_DATA && event->data != nullptr &&
      event->data_len > 0) {
    const auto len = static_cast<std::size_t>(event->data_len);
    if (collector->body.size() + len > kMaxBodyBytes) {
      collector->overflow = true;
      return ESP_FAIL;  // 主动中止
    }
    collector->body.append(static_cast<const char*>(event->data), len);
  }
  return ESP_OK;
}

}  // namespace

HttpResponse HttpGet(const std::string& url, uint32_t timeout_ms) {
  HttpResponse response;

  if (!IsNetworkUp()) {
    response.error = "网络未连接";
    return response;
  }
  if (url.empty()) {
    response.error = "URL 为空";
    return response;
  }

  BodyCollector collector;

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = static_cast<int>(timeout_ms);
  config.event_handler = OnHttpEvent;
  config.user_data = &collector;
  config.disable_auto_redirect = false;
  config.buffer_size = 1024;
  config.buffer_size_tx = 1024;
  // HTTPS 必须显式给出服务器校验方式，否则 esp-tls 直接拒绝建连
  // （"No server verification option set in esp_tls_cfg_t structure"）。
  // 这里挂 ESP-IDF 内置的根证书包（cacrt_all.pem，145 张根），既完成校验
  // 又不占我们的 flash —— 证书包由组件提供。
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    response.error = "esp_http_client_init 失败";
    return response;
  }

  esp_http_client_set_header(client, "User-Agent", "dida-clock/1.0");
  esp_http_client_set_header(client, "Accept", "application/json");
  esp_http_client_set_header(client, "Connection", "close");

  const esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    if (collector.overflow) {
      response.error = "响应体超过上限";
    } else {
      char buffer[128];
      std::snprintf(buffer, sizeof(buffer), "请求失败: %s",
                    esp_err_to_name(err));
      response.error = buffer;
    }
    esp_http_client_cleanup(client);
    return response;
  }

  response.status = esp_http_client_get_status_code(client);
  response.body = std::move(collector.body);
  response.ok = true;
  esp_http_client_cleanup(client);

  ESP_LOGI(kTag, "GET %s -> %d（%u 字节原始响应体）", RedactUrl(url).c_str(),
           response.status, static_cast<unsigned>(response.body.size()));
  return response;
}

}  // namespace net
