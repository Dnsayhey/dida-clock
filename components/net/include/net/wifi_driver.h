#pragma once

#include "esp_err.h"

namespace net {

// 确保 WiFi 驱动就绪：网络栈 + 默认事件循环 + esp_wifi_init()。
//
// 幂等：重复调用只做检查，不会重复初始化。已初始化时返回 ESP_OK。
//
// 为什么必须独立成一个函数（而不是藏在 WifiStation::Begin() 里）：
//
//   WiFi 有两个互斥的使用路径 ——
//     * 已配置 Wi-Fi  -> WifiStation::Begin() 走 STA 连接
//     * 未配置 Wi-Fi  -> 强制门户自己调 esp_wifi_set_mode(WIFI_MODE_AP)
//
//   驱动初始化是两条路径**共同的前提**，但只有 STA 那条会调 Begin()。
//   如果初始化留在 Begin() 里，配网模式下 esp_wifi_init() 就不会被执行，
//   门户只能拿到 ESP_ERR_WIFI_NOT_INIT。放在装配层调用一次才可靠。
esp_err_t EnsureWifiDriverReady();

}  // namespace net
