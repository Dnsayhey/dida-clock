#pragma once

#include <cstdint>
#include <string>

namespace net {

struct HttpResponse {
  bool ok = false;    // 传输层是否成功（拿到完整响应体）
  int status = 0;     // HTTP 状态码；未拿到响应时为 0
  std::string body;   // **原始**响应体字节，可能仍是 gzip 压缩的
  std::string error;  // ok=false 时的诊断信息

  bool IsHttpOk() const { return ok && status >= 200 && status < 300; }
};

// 发起一次 GET 请求。
//
// 关于压缩：esp_http_client **没有任何 gzip/deflate 支持**（已核实其
// headers/src/Kconfig 中零提及），所以这里一律返回原始字节，
// 由上层用 decompress 组件解压。
//
// 不主动发送 Accept-Encoding —— 和风天气无条件返回 gzip，
// 发了也不会改变行为。
HttpResponse HttpGet(const std::string& url, uint32_t timeout_ms);

}  // namespace net
