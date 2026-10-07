#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace app {

// 本文件放"运行期状态发布"：net 任务写、UI 任务读。
//
// 两者都遵守 CONVENTIONS §5：**锁保护的快照访问器**，读侧拿拷贝、锁外使用，
// 不返回内部引用。

// ---------------- 启动阶段 ----------------
//
// 用于 INIT 页显示真实启动进度，而不是一句静态的"正在启动"。
enum class StartupPhase {
  kBoot = 0,
  kLoadingConfig,     // 读 NVS 配置
  kWaitingForConfig,  // 等用户在强制门户里填配置
  kConnectingWifi,    // 正在连接 WiFi
  kSyncingTime,       // 等待 SNTP 对时
  kSyncingWeather,    // 解析位置 + 首次天气同步
  kReady,             // 就绪，已切到天气页
  kFailed,
};

const char* StartupPhaseName(StartupPhase phase);

struct StartupStatusSnapshot {
  StartupPhase phase = StartupPhase::kBoot;
  std::string detail;  // 附加说明（SSID、失败原因等）
  uint32_t sequence = 0;
};

class StartupStatus {
 public:
  void Set(StartupPhase phase, std::string detail = {});
  StartupStatusSnapshot Snapshot() const;

 private:
  mutable std::mutex mutex_;
  StartupStatusSnapshot data_;
};

// ---------------- 配网信息 ----------------
//
// NETWORK_SETUP 页要显示"连哪个热点、密码多少、浏览器开哪个地址"。
// 这些信息由 net 任务在启动门户后发布，页面只读 app 层，不认识 portal 组件。
struct PortalStatusSnapshot {
  bool running = false;
  std::string ssid;
  std::string password;
  std::string ip;
};

class PortalStatus {
 public:
  void Publish(bool running, std::string ssid, std::string password,
               std::string ip);
  PortalStatusSnapshot Snapshot() const;

 private:
  mutable std::mutex mutex_;
  PortalStatusSnapshot data_;
};

// 全局实例。net 任务写、UI 任务读 —— 与 net::GetNetworkStatus() 同一模式。
StartupStatus& GetStartupStatus();
PortalStatus& GetPortalStatus();

}  // namespace app
