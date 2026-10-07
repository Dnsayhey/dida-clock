#include "startup_flow.h"

#include <esp_log.h>

#include "app/runtime_status.h"
#include "portal/portal_page.h"
#include "runtime.h"

namespace dida {
namespace {

constexpr char kTag[] = "dida";

}  // namespace

bool StartPortal() {
  app::GetStartupStatus().Set(app::StartupPhase::kWaitingForConfig);

  if (!g_portal.Begin(&SaveProvisionedConfig)) {
    ESP_LOGE(kTag, "[net] 强制门户启动失败，配网不可用");
    app::GetStartupStatus().Set(app::StartupPhase::kFailed,
                                "Setup hotspot failed");
    return false;
  }

  // 发布给 app 层，配网页只读这里，不认识 portal 组件
  app::GetPortalStatus().Publish(true, g_portal.Ssid(),
                                 portal::kPortalApPassword, g_portal.Ip());

  ESP_LOGI(kTag, "[net] 请连接热点 \"%s\"（密码 %s），浏览器打开 http://%s",
           g_portal.Ssid().c_str(), portal::kPortalApPassword,
           g_portal.Ip().c_str());
  return true;
}

bool SaveProvisionedConfig(const config::DeviceConfig& incoming) {
  config::DeviceConfig merged = g_config_store.Load();
  merged.wifi_ssid = incoming.wifi_ssid;
  merged.wifi_password = incoming.wifi_password;
  merged.adm = incoming.adm;
  merged.location = incoming.location;
  // 位置变了，之前解析出来的 location_id / 经纬度作废，留给下次启动重新解析
  merged.location_id.clear();
  merged.location_lat.clear();
  merged.location_lon.clear();

  const esp_err_t err = g_config_store.Save(merged);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "配网配置写入 NVS 失败: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(kTag, "配网配置已写入 NVS: ssid=\"%s\" location=\"%s\"",
           merged.wifi_ssid.c_str(), merged.location.c_str());
  return true;
}

void EnterInitialPage(bool portal_mode, NavigationRequest on_navigate) {
  if (portal_mode) {
    on_navigate(app::PageType::kNetworkSetup);
  } else {
    app::GetStartupStatus().Set(app::StartupPhase::kReady);
    // 切页日志由 PageManager::SwitchTo() 统一记录，这里不重复打。
    on_navigate(app::PageType::kRealTimeWeather);
  }
}

}  // namespace dida
