#include "ota_update.h"

#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <cstdio>
#include <string>

#include "app/runtime_status.h"
#include "ota_config.h"

#if DIDA_HAS_OTA_CONFIG_LOCAL
#include <esp_crt_bundle.h>
#include <esp_https_ota.h>
#include <esp_system.h>

#include "cJSON.h"
#include "net/http_client.h"
#endif

namespace dida {
namespace {

constexpr char kTag[] = "ota";
constexpr uint32_t kManifestTimeoutMs = 3000;

#if DIDA_HAS_OTA_CONFIG_LOCAL
struct Manifest {
  std::string version;
  std::string url;
  int size = 0;
};

bool ParseVersion(const std::string& text, std::array<int, 3>& out) {
  int part = 0;
  int value = 0;
  bool has_digit = false;
  for (char c : text) {
    if (c >= '0' && c <= '9') {
      value = value * 10 + (c - '0');
      has_digit = true;
      if (value > 1000000) {
        return false;
      }
      continue;
    }
    if (c != '.' || !has_digit || part >= 2) {
      return false;
    }
    out[part++] = value;
    value = 0;
    has_digit = false;
  }
  if (!has_digit || part != 2) {
    return false;
  }
  out[part] = value;
  return true;
}

bool IsNewerVersion(const std::string& candidate, const std::string& current) {
  std::array<int, 3> next = {};
  std::array<int, 3> running = {};
  if (!ParseVersion(candidate, next) || !ParseVersion(current, running)) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    if (next[i] != running[i]) {
      return next[i] > running[i];
    }
  }
  return false;
}

void SetStatus(const std::string& detail) {
  app::GetStartupStatus().Set(app::StartupPhase::kSyncingTime, detail);
}

bool FetchManifest(Manifest& out) {
  SetStatus("检查更新");
  const net::HttpResponse response =
      net::HttpGet(DIDA_OTA_MANIFEST_URL, kManifestTimeoutMs);
  if (!response.IsHttpOk()) {
    ESP_LOGW(kTag, "manifest 请求失败: %s", response.error.c_str());
    SetStatus("检查更新失败，继续启动");
    return false;
  }

  cJSON* root = cJSON_Parse(response.body.c_str());
  if (root == nullptr) {
    ESP_LOGW(kTag, "manifest JSON 解析失败");
    SetStatus("检查更新失败，继续启动");
    return false;
  }

  const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "version");
  const cJSON* url = cJSON_GetObjectItemCaseSensitive(root, "url");
  const cJSON* size = cJSON_GetObjectItemCaseSensitive(root, "size");
  const bool valid = cJSON_IsString(version) &&
                     version->valuestring != nullptr && cJSON_IsString(url) &&
                     url->valuestring != nullptr;
  if (valid) {
    out.version = version->valuestring;
    out.url = url->valuestring;
    if (cJSON_IsNumber(size)) {
      out.size = size->valueint;
    }
  }
  cJSON_Delete(root);

  if (!valid || out.version.size() > 31 || out.url.empty()) {
    ESP_LOGW(kTag, "manifest 缺少 version/url");
    SetStatus("检查更新失败，继续启动");
    return false;
  }
  return true;
}

bool DownloadAndWrite(const Manifest& manifest) {
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  if (target == nullptr) {
    ESP_LOGE(kTag, "没有可用的 OTA 分区");
    return false;
  }
  if (manifest.size > 0 &&
      static_cast<uint32_t>(manifest.size) > target->size) {
    ESP_LOGE(kTag, "固件 %d 字节超过分区 %u 字节", manifest.size,
             static_cast<unsigned>(target->size));
    SetStatus("固件太大，继续启动");
    return false;
  }

  esp_http_client_config_t http_config = {};
  http_config.url = manifest.url.c_str();
  http_config.timeout_ms = 15000;
  http_config.keep_alive_enable = true;
  // 本地 HTTP 测试时该字段不参与校验；正式 HTTPS 直接复用天气的证书包。
  http_config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_https_ota_config_t ota_config = {};
  ota_config.http_config = &http_config;

  esp_https_ota_handle_t handle = nullptr;
  esp_err_t err = esp_https_ota_begin(&ota_config, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "OTA begin 失败: %s", esp_err_to_name(err));
    SetStatus("更新准备失败，继续启动");
    return false;
  }

  const int total = esp_https_ota_get_image_size(handle);
  ESP_LOGI(kTag, "OTA begin 成功，镜像 %d 字节", total);
  int last_percent = -1;
  while (true) {
    err = esp_https_ota_perform(handle);
    if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      break;
    }

    const int received = esp_https_ota_get_image_len_read(handle);
    if (total > 0 && received >= 0) {
      const int percent =
          static_cast<int>((static_cast<int64_t>(received) * 100) / total);
      if (percent != last_percent) {
        last_percent = percent;
        char detail[32];
        std::snprintf(detail, sizeof(detail), "更新固件 %d%%", percent);
        SetStatus(detail);
        if (percent % 10 == 0) {
          ESP_LOGI(kTag, "OTA 进度 %d%%（%d/%d）", percent, received, total);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
    ESP_LOGE(kTag, "OTA 下载失败: %s（complete=%d）", esp_err_to_name(err),
             esp_https_ota_is_complete_data_received(handle));
    esp_https_ota_abort(handle);
    SetStatus("更新失败，继续启动");
    return false;
  }

  err = esp_https_ota_finish(handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "OTA finish 失败: %s", esp_err_to_name(err));
    SetStatus("更新失败，继续启动");
    return false;
  }

  ESP_LOGI(kTag, "OTA 写入完成，准备重启");
  SetStatus("更新完成，重启");
  return true;
}
#endif  // DIDA_HAS_OTA_CONFIG_LOCAL

}  // namespace

void RunOtaUpdateIfConfigured() {
#if DIDA_HAS_OTA_CONFIG_LOCAL
  ESP_LOGI(kTag, "OTA build: %s", DIDA_OTA_BUILD_LABEL);
  if (DIDA_OTA_MANIFEST_URL[0] == '\0') {
    ESP_LOGI(kTag, "未配置 manifest URL，跳过");
    return;
  }

  Manifest manifest;
  if (!FetchManifest(manifest)) {
    return;
  }

  const std::string current = esp_app_get_description()->version;
  if (!IsNewerVersion(manifest.version, current)) {
    ESP_LOGI(kTag, "当前 %s，远端 %s，无需更新", current.c_str(),
             manifest.version.c_str());
    SetStatus("已是最新");
    return;
  }

  ESP_LOGI(kTag, "发现新版本 %s（当前 %s），开始更新", manifest.version.c_str(),
           current.c_str());
  SetStatus("发现新版本");
  if (DownloadAndWrite(manifest)) {
    esp_restart();
  }
#else
  // 正式固件没有本地 manifest 配置，不链接 OTA 下载路径。
#endif
}

void MarkOtaAppValidIfNeeded() {
  const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
    return;  // 本次不是 OTA 启动，正常情况
  }
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "标记 OTA 固件有效失败: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(kTag, "OTA 固件已标记为有效");
  }
}

}  // namespace dida
