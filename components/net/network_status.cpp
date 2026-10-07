#include "net/network_status.h"

#include <mutex>

namespace net {
namespace {

std::mutex g_mutex;
NetworkStatus g_status;

}  // namespace

void PublishNetworkStatus(const NetworkStatus& status) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status = status;
}

NetworkStatus GetNetworkStatus() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_status;  // 锁内完成拷贝，出锁后调用方独占
}

}  // namespace net
