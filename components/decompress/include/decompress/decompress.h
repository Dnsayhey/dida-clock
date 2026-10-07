#pragma once

// 响应体解压。
//
// 背景（已用真实响应核实，非推测）：
//   * 和风天气**无条件**返回 gzip 压缩的响应体 —— 即使不发 Accept-Encoding，
//     响应也带 `content-encoding: gzip`，体以 1f 8b 开头。
//   * ESP-IDF **没有内置 zlib/miniz 源码**，只提供 ESP32-C3 ROM 里 miniz 的
//     声明头（esp_rom/include/miniz.h）。ROM 里只有 tinfl_* 系列，
//     没有 mz_uncompress / mz_inflateInit2。
//   * tinfl 只支持「裸 deflate」与「zlib 头」，**不直接支持 gzip 容器**，
//     所以 gzip 的可选字段必须由我们自己解析后跳过。
//
// 因此本组件分两部分：
//   * decompress_format.cpp —— 纯逻辑的格式识别与 gzip 头解析，**主机可单测**；
//   * decompress_miniz.cpp  —— 设备侧调用 ROM miniz 的 inflate。
// 这样风险最高的部分（头部解析）能被真实数据覆盖，而不是只能上机试。

#include <cstddef>
#include <cstdint>
#include <string>

namespace decompress {

enum class Format {
  kPlain,  // 不是压缩数据（直接是 JSON 文本）
  kGzip,   // gzip 容器（1f 8b）
  kZlib,   // zlib 容器（RFC1950）
  kUnknown,
};

const char* FormatName(Format format);

// 纯逻辑：仅依据头部字节判定格式。
// 非 gzip、非 zlib 一律视为 kPlain —— 让后续 JSON 解析去报明确的错，
// 而不是在这里抛出含糊的"压缩格式错误"。
Format Detect(const uint8_t* data, std::size_t size);

// 纯逻辑：计算 gzip 流中裸 deflate 数据的起始偏移。
// 会按 FLG 位跳过 FEXTRA / FNAME / FCOMMENT / FHCRC 等可选字段。
// 头部不完整、CM 非 deflate、或设置了保留位时返回 false。
bool GzipDeflateOffset(const uint8_t* data, std::size_t size,
                       std::size_t& offset);

// 解压。输入可以是 gzip / zlib / 未压缩文本；输出解压后的字节。
// 失败时返回 false 并填充 error（可直接用于日志或 SyncState.message）。
bool Inflate(const uint8_t* data, std::size_t size, std::string& out,
             std::string& error);

// 便利重载。
inline bool Inflate(const std::string& input, std::string& out,
                    std::string& error) {
  return Inflate(reinterpret_cast<const uint8_t*>(input.data()), input.size(),
                 out, error);
}

}  // namespace decompress
