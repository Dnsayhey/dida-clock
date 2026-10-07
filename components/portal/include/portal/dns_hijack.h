#pragma once

#include <cstddef>
#include <cstdint>

namespace portal {

// 设备侧：监听 UDP 53，把收到的每个查询都应答为 ip。
class DnsHijack {
 public:
  ~DnsHijack();

  DnsHijack() = default;
  DnsHijack(const DnsHijack&) = delete;
  DnsHijack& operator=(const DnsHijack&) = delete;

  bool Begin(uint32_t ip_be, uint16_t port = 53);
  void Stop();

  // 处理一个待到达的查询。
  // 返回 true 表示"这一轮还有包可处理"（继续调用），false 表示已排空。
  // 套接字是**非阻塞**的：空闲时立即返回 false，不会拖慢调用它的任务。
  bool PollOnce();

  bool Running() const { return socket_ >= 0; }

 private:
  int socket_ = -1;
  uint32_t ip_be_ = 0;
};

}  // namespace portal
