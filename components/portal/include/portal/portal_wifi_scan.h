#pragma once

#include <string>
#include <vector>

namespace portal {

// 扫描周边 WiFi，返回去重后的 SSID 列表（空 SSID 会被跳过）。
//
// **调用契约**：
//   * 扫描需要 WiFi 处于已启动状态，所以本函数内部会切到 STA 并
//     esp_wifi_start()，扫完再 esp_wifi_stop()；
//   * 因此调用方必须让 WiFi 处于停止状态再调用，并且**扫完再从停止状态**
//     去启动 AP —— 否则会撞上状态机的"已启动"错误；
//   * 扫描是阻塞的（约 2 秒），只能在 net 任务里调用。
std::vector<std::string> ScanWifiOptions();

}  // namespace portal
