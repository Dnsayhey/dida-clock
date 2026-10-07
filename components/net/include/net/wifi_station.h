#pragma once

#include <esp_err.h>

#include <cstdint>
#include <string>

namespace net {

// Wi-Fi STA 连接。
//
// 接口刻意收敛成"初始化一次 + 连接一次"：初始化与连接分离，
// 重连只需再调 Connect()，不会再碰 Wi-Fi 驱动本身。
//
// 注意 ESP-IDF 6.0 的两处破坏性变更：
//   * 必须用 WIFI_IF_STA 枚举，ESP_IF_WIFI_STA 宏已移除；
//   * esp_wifi_init 只能成功调用一次，重复调用返回 ESP_ERR_INVALID_STATE。
//     因此 Begin() 用幂等标志保护；切换运行模式请用 esp_wifi_set_mode 而不是重复 init。
class WifiStation {
 public:
  // 初始化 netif / 事件循环 / Wi-Fi 驱动，并设为 STA 模式后启动。
  // 可重复调用：已初始化时直接返回 ESP_OK。
  esp_err_t Begin();

  // 连接指定 AP 并等待拿到 IP。
  // timeout_ms 内未拿到 IP 则返回 false（内部会断开，避免半连接状态）。
  bool Connect(const std::string& ssid, const std::string& password,
               uint32_t timeout_ms);

  // 断开当前连接（不停止 Wi-Fi 驱动）。
  void Disconnect();

  bool IsConnected() const;

  // 当前 IP 的字符串形式；未连接时返回空串。
  std::string IpAddress() const;

  // RSSI（dBm）；未连接时返回 0。
  int Rssi() const;

 private:
  bool initialized_ = false;
};

// 全局网络是否可用（已连接且拿到 IP）。
// 供 HTTP 层使用，避免 HTTP 层直接依赖 WifiStation。
bool IsNetworkUp();

}  // namespace net
