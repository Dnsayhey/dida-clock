#include "portal/dns_hijack.h"

#include <arpa/inet.h>
#include <esp_log.h>
#include <fcntl.h>
#include <lwip/sockets.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "portal/dns_message.h"

namespace portal {
namespace {
constexpr char kTag[] = "dns_hijack";
}  // namespace

DnsHijack::~DnsHijack() { Stop(); }

bool DnsHijack::Begin(uint32_t ip_be, uint16_t port) {
  if (socket_ >= 0) {
    return true;
  }

  socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket_ < 0) {
    ESP_LOGE(kTag, "创建 UDP 套接字失败: errno %d", errno);
    return false;
  }

  sockaddr_in bind_addr = {};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind_addr.sin_port = htons(port);
  if (::bind(socket_, reinterpret_cast<sockaddr*>(&bind_addr),
             sizeof(bind_addr)) < 0) {
    ESP_LOGE(kTag, "绑定 UDP %u 失败: errno %d", static_cast<unsigned>(port),
             errno);
    ::close(socket_);
    socket_ = -1;
    return false;
  }

  ip_be_ = ip_be;

  // 非阻塞：Poll 由 net 任务周期调用，空闲时必须立刻返回，
  // 不能让 DNS 的等待时间占用网络任务。
  const int flags = ::fcntl(socket_, F_GETFL, 0);
  ::fcntl(socket_, F_SETFL, flags | O_NONBLOCK);

  ESP_LOGI(kTag, "通配 DNS 已启动: UDP %u", static_cast<unsigned>(port));
  return true;
}

bool DnsHijack::PollOnce() {
  if (socket_ < 0) {
    return false;
  }

  uint8_t query[512];
  sockaddr_in from = {};
  socklen_t from_len = sizeof(from);
  const int received =
      ::recvfrom(socket_, query, sizeof(query), 0,
                 reinterpret_cast<sockaddr*>(&from), &from_len);
  if (received <= 0) {
    return false;  // 已排空（EAGAIN）或出错
  }

  uint8_t response[512];
  std::size_t response_len = 0;
  if (!BuildDnsResponse(query, static_cast<std::size_t>(received), ip_be_,
                        response, sizeof(response), response_len)) {
    return true;  // 收到了但不需要应答，继续排空
  }

  ::sendto(socket_, response, response_len, 0,
           reinterpret_cast<sockaddr*>(&from), from_len);
  return true;
}

void DnsHijack::Stop() {
  if (socket_ >= 0) {
    ::close(socket_);
    socket_ = -1;
    ESP_LOGI(kTag, "通配 DNS 已停止");
  }
}

}  // namespace portal
