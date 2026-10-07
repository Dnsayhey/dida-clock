#include "storage/nvs_config_store.h"

#include <nvs.h>
#include <nvs_flash.h>

#include <string>
#include <vector>

#include "esp_log.h"

namespace storage {
namespace {

constexpr char kTag[] = "nvs_config";

// 一次 NVS 会话：构造时打开命名空间，析构时关闭。
class NvsSession {
 public:
  explicit NvsSession(bool read_only) : read_only_(read_only) {
    // 命名空间不存在时，READWRITE 打开会创建它。
    // 这样整片 flash 被擦除后（命名空间不存在）也能正常工作。
    result_ = nvs_open(config::kNvsNamespace,
                       read_only ? NVS_READONLY : NVS_READWRITE, &handle_);
  }

  ~NvsSession() {
    if (result_ == ESP_OK) {
      nvs_close(handle_);
    }
  }

  NvsSession(const NvsSession&) = delete;
  NvsSession& operator=(const NvsSession&) = delete;

  bool Ok() const { return result_ == ESP_OK; }
  esp_err_t Result() const { return result_; }
  nvs_handle_t handle() const { return handle_; }

 private:
  bool read_only_;
  nvs_handle_t handle_ = 0;
  esp_err_t result_ = ESP_FAIL;
};

std::string ReadString(nvs_handle_t handle, const char* key) {
  std::size_t length = 0;
  esp_err_t err = nvs_get_str(handle, key, nullptr, &length);
  if (err != ESP_OK || length == 0) {
    return {};
  }

  // nvs_get_str 的 length 含结尾 NUL
  std::vector<char> buffer(length);
  err = nvs_get_str(handle, key, buffer.data(), &length);
  if (err != ESP_OK) {
    return {};
  }
  return std::string(buffer.data());
}

int ReadInt(nvs_handle_t handle, const char* key, int fallback) {
  int32_t value = 0;
  if (nvs_get_i32(handle, key, &value) != ESP_OK) {
    return fallback;
  }
  return static_cast<int>(value);
}

void WriteString(nvs_handle_t handle, const char* key,
                 const std::string& value) {
  if (value.empty()) {
    // 空值一律走 erase，而不是写空串：让"键不存在"成为"未配置"的唯一表示，
    // 读侧不必再去区分"存在但为空"和"不存在"两种状态。
    nvs_erase_key(handle, key);
    return;
  }
  nvs_set_str(handle, key, value.c_str());
}

}  // namespace

esp_err_t NvsConfigStore::Begin() {
  NvsSession session(false);
  if (!session.Ok()) {
    ESP_LOGE(kTag, "打开 NVS 命名空间 '%s' 失败: %s", config::kNvsNamespace,
             esp_err_to_name(session.Result()));
    return session.Result();
  }
  return ESP_OK;
}

config::DeviceConfig NvsConfigStore::Load() const {
  config::DeviceConfig out;

  NvsSession session(true);
  if (!session.Ok()) {
    ESP_LOGW(kTag, "读取配置失败，返回默认值: %s",
             esp_err_to_name(session.Result()));
    return out;
  }

  const nvs_handle_t handle = session.handle();
  out.wifi_ssid = ReadString(handle, config::kKeyWifiSsid);
  out.wifi_password = ReadString(handle, config::kKeyWifiPassword);
  out.adm = ReadString(handle, config::kKeyAdm);
  out.location = ReadString(handle, config::kKeyLocation);
  out.location_id = ReadString(handle, config::kKeyLocationId);
  out.location_lat = ReadString(handle, config::kKeyLocationLat);
  out.location_lon = ReadString(handle, config::kKeyLocationLon);
  // 缺键时回退到 DeviceConfig 的字段默认值（而不是再写一份字面量）：
  // 默认值只有一个来源，改那里就等于改"新设备开机是什么档位/主题"。
  out.backlight_mode =
      ReadInt(handle, config::kKeyBacklightMode, out.backlight_mode);
  out.theme_mode = ReadInt(handle, config::kKeyThemeMode, out.theme_mode);
  return out;
}

esp_err_t NvsConfigStore::Save(const config::DeviceConfig& input) {
  const config::DeviceConfig normalized = config::Normalize(input);

  NvsSession session(false);
  if (!session.Ok()) {
    ESP_LOGE(kTag, "写入配置失败，无法打开命名空间: %s",
             esp_err_to_name(session.Result()));
    return session.Result();
  }

  const nvs_handle_t handle = session.handle();
  WriteString(handle, config::kKeyWifiSsid, normalized.wifi_ssid);
  WriteString(handle, config::kKeyWifiPassword, normalized.wifi_password);
  WriteString(handle, config::kKeyAdm, normalized.adm);
  WriteString(handle, config::kKeyLocation, normalized.location);
  WriteString(handle, config::kKeyLocationId, normalized.location_id);
  WriteString(handle, config::kKeyLocationLat, normalized.location_lat);
  WriteString(handle, config::kKeyLocationLon, normalized.location_lon);
  nvs_set_i32(handle, config::kKeyBacklightMode, normalized.backlight_mode);
  nvs_set_i32(handle, config::kKeyThemeMode, normalized.theme_mode);

  const esp_err_t err = nvs_commit(handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs_commit 失败: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t NvsConfigStore::SaveBrightnessMode(int mode) {
  NvsSession session(false);
  if (!session.Ok()) {
    return session.Result();
  }
  nvs_set_i32(session.handle(), config::kKeyBacklightMode, mode);
  return nvs_commit(session.handle());
}

esp_err_t NvsConfigStore::SaveThemeMode(int mode) {
  NvsSession session(false);
  if (!session.Ok()) {
    return session.Result();
  }
  nvs_set_i32(session.handle(), config::kKeyThemeMode, mode);
  return nvs_commit(session.handle());
}

esp_err_t NvsConfigStore::FactoryReset() {
  NvsSession session(false);
  if (!session.Ok()) {
    ESP_LOGE(kTag, "恢复出厂失败，无法打开命名空间: %s",
             esp_err_to_name(session.Result()));
    return session.Result();
  }

  esp_err_t err = nvs_erase_all(session.handle());
  if (err == ESP_OK) {
    err = nvs_commit(session.handle());
  }
  ESP_LOGI(kTag, "恢复出厂: %s", esp_err_to_name(err));
  return err;
}

}  // namespace storage
