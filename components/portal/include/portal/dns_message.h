#pragma once

#include <cstddef>
#include <cstdint>

namespace portal {

// 构造 DNS 应答：把查询里的名字一律解析为 ip_be（**网络字节序**）。
//
// 这是通配 DNS 劫持的核心 —— 强制门户要求任意域名都指向本机，
// 这样手机探测联网状态时必然打到我们的 HTTP 服务器上。
//
// **纯逻辑**（单独成文件，见 CONVENTIONS §1.3）：不碰套接字、不依赖 ESP-IDF，
// 因此可以在主机上逐字节验证报文格式。
//
// 返回 false 表示"这不是一个该应答的查询"（已是响应、问题段数量不为 1、
// 含压缩指针、缓冲区不足等）。
//
// 限制：只处理标准单问题查询；应答段的 TYPE 写死为 A（见 dns_message.cpp），
// 因此对 AAAA 等其它 QTYPE 返回的也是 A 记录。
bool BuildDnsResponse(const uint8_t* query, std::size_t query_len,
                      uint32_t ip_be, uint8_t* out, std::size_t out_cap,
                      std::size_t& out_len);

}  // namespace portal
