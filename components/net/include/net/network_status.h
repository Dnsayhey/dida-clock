#pragma once

#include <string>

namespace net {

// 网络状态的线程安全发布/读取。
//
// 为什么需要它：页面要显示 IP，而该信息由 net 任务在事件回调里更新、
// 由 UI 任务读取。直接共享一个 bool/std::string 就是跨任务无保护访问，
// 会读到正在被改写的 std::string（撕裂 / use-after-free）。
//
// 策略与 WeatherStore 相同：写侧整体替换、读侧返回拷贝，内部单锁，
// 且读侧拿到拷贝后在锁外使用。
struct NetworkStatus {
  bool connected = false;
  std::string ip;
  std::string ssid;
  int rssi = 0;  // dBm，未连接时为 0
};

// 由 net 任务调用。
void PublishNetworkStatus(const NetworkStatus& status);

// 任意任务可调用（返回拷贝）。
NetworkStatus GetNetworkStatus();

}  // namespace net
