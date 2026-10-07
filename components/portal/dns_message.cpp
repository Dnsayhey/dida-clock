#include "portal/dns_message.h"

#include <cstring>

namespace portal {

bool BuildDnsResponse(const uint8_t* query, std::size_t query_len,
                      uint32_t ip_be, uint8_t* out, std::size_t out_cap,
                      std::size_t& out_len) {
  if (query == nullptr || out == nullptr || query_len < 12) {
    return false;
  }

  const uint16_t flags = static_cast<uint16_t>((query[2] << 8) | query[3]);
  if ((flags & 0x8000) != 0) {
    return false;  // 已经是响应，不应答（避免回环）
  }

  const uint16_t qdcount = static_cast<uint16_t>((query[4] << 8) | query[5]);
  if (qdcount != 1) {
    return false;  // 只处理标准单问题查询
  }

  // 定位问题段结束：NAME（若干标签，以 0 结尾）+ QTYPE(2) + QCLASS(2)
  std::size_t pos = 12;
  while (pos < query_len && query[pos] != 0) {
    if ((query[pos] & 0xC0) == 0xC0) {
      return false;  // 问题段不该出现压缩指针
    }
    pos += static_cast<std::size_t>(query[pos]) + 1;
  }
  if (pos + 1 + 4 > query_len) {
    return false;  // 问题段不完整
  }
  const std::size_t question_end = pos + 1 + 4;

  // 应答段固定 16 字节：NAME(2) TYPE(2) CLASS(2) TTL(4) RDLENGTH(2) RDATA(4)
  const std::size_t need = question_end + 16;
  if (need > out_cap) {
    return false;
  }

  std::memcpy(out, query, question_end);

  // 头部：QR=1 AA=1 RD=1 RA=1 RCODE=0；ANCOUNT=1，其余计数清零
  out[2] = 0x81;
  out[3] = 0x80;
  out[6] = 0x00;
  out[7] = 0x01;  // ANCOUNT
  out[8] = 0x00;
  out[9] = 0x00;  // NSCOUNT
  out[10] = 0x00;
  out[11] = 0x00;  // ARCOUNT

  std::size_t o = question_end;
  out[o++] = 0xC0;  // NAME：压缩指针指向偏移 12 处的问题名
  out[o++] = 0x0C;
  out[o++] = 0x00;
  out[o++] = 0x01;  // TYPE = A
  out[o++] = 0x00;
  out[o++] = 0x01;  // CLASS = IN
  out[o++] = 0x00;  // TTL = 60s
  out[o++] = 0x00;
  out[o++] = 0x00;
  out[o++] = 0x3C;
  out[o++] = 0x00;
  out[o++] = 0x04;  // RDLENGTH = 4
  std::memcpy(out + o, &ip_be, 4);
  o += 4;

  out_len = o;
  return true;
}

}  // namespace portal
