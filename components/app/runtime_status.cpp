#include "app/runtime_status.h"

#include <utility>

namespace app {

const char* StartupPhaseName(StartupPhase phase) {
  switch (phase) {
    case StartupPhase::kBoot:
      return "正在启动";
    case StartupPhase::kLoadingConfig:
      return "读取配置";
    case StartupPhase::kWaitingForConfig:
      return "等待配网";
    case StartupPhase::kConnectingWifi:
      return "连接网络";
    case StartupPhase::kSyncingTime:
      return "同步时间";
    case StartupPhase::kSyncingWeather:
      return "同步天气";
    case StartupPhase::kReady:
      return "已就绪";
    case StartupPhase::kFailed:
      return "启动失败";
  }
  return "未知";
}

void StartupStatus::Set(StartupPhase phase, std::string detail) {
  std::lock_guard<std::mutex> lock(mutex_);
  data_.phase = phase;
  data_.detail = std::move(detail);
  ++data_.sequence;
}

StartupStatusSnapshot StartupStatus::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return data_;  // 按值返回：读侧锁外自由使用
}

void PortalStatus::Publish(bool running, std::string ssid, std::string password,
                           std::string ip) {
  std::lock_guard<std::mutex> lock(mutex_);
  data_.running = running;
  data_.ssid = std::move(ssid);
  data_.password = std::move(password);
  data_.ip = std::move(ip);
}

PortalStatusSnapshot PortalStatus::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return data_;
}

StartupStatus& GetStartupStatus() {
  static StartupStatus instance;
  return instance;
}

PortalStatus& GetPortalStatus() {
  static PortalStatus instance;
  return instance;
}

}  // namespace app
