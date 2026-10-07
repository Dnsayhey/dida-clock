// 纯逻辑部分：格式识别与 gzip 头解析。
//
// 这个文件**不依赖 ESP-IDF**，因此可以在主机上直接单测。
// 之所以要单独拆出来：gzip 头部解析是本组件里风险最高的部分
// （解析错误会静默产出垃圾数据或越界），必须能被真实压缩数据覆盖。

#include "decompress/decompress.h"

namespace decompress {
namespace {

// gzip 固定头长度：ID1 ID2 CM FLG MTIME(4) XFL OS
constexpr std::size_t kGzipFixedHeaderSize = 10;

constexpr uint8_t kGzipId1 = 0x1F;
constexpr uint8_t kGzipId2 = 0x8B;
constexpr uint8_t kGzipCmDeflate = 8;

// FLG 各位
constexpr uint8_t kFlgFhcrc = 0x02;
constexpr uint8_t kFlgFextra = 0x04;
constexpr uint8_t kFlgFname = 0x08;
constexpr uint8_t kFlgFcomment = 0x10;
constexpr uint8_t kFlgReserved = 0xE0;

// zlib 头（RFC1950）：CMF 低 4 位为压缩方法(8=deflate)，
// CMF 高 4 位为窗口大小(<=7)，且 (CMF<<8|FLG) 必须是 31 的整数倍。
bool IsZlibHeader(uint8_t cmf, uint8_t flg) {
  if ((cmf & 0x0F) != kGzipCmDeflate) {
    return false;
  }
  if ((cmf >> 4) > 7) {
    return false;
  }
  return ((static_cast<unsigned>(cmf) << 8) | flg) % 31 == 0;
}

// 跳过以 NUL 结尾的字符串（FNAME / FCOMMENT 共用）
bool SkipZeroTerminated(const uint8_t* data, std::size_t size,
                        std::size_t& pos) {
  while (pos < size && data[pos] != 0) {
    ++pos;
  }
  if (pos >= size) {
    return false;  // 没找到结尾的 NUL，头不完整
  }
  ++pos;  // 跳过 NUL 本身
  return true;
}

}  // namespace

const char* FormatName(Format format) {
  switch (format) {
    case Format::kPlain:
      return "plain";
    case Format::kGzip:
      return "gzip";
    case Format::kZlib:
      return "zlib";
    case Format::kUnknown:
      return "unknown";
  }
  return "unknown";
}

Format Detect(const uint8_t* data, std::size_t size) {
  if (data == nullptr || size < 2) {
    return size == 0 ? Format::kUnknown : Format::kPlain;
  }
  if (data[0] == kGzipId1 && data[1] == kGzipId2) {
    return Format::kGzip;
  }
  if (IsZlibHeader(data[0], data[1])) {
    return Format::kZlib;
  }
  return Format::kPlain;
}

bool GzipDeflateOffset(const uint8_t* data, std::size_t size,
                       std::size_t& offset) {
  if (data == nullptr || size < kGzipFixedHeaderSize) {
    return false;
  }
  if (data[0] != kGzipId1 || data[1] != kGzipId2) {
    return false;
  }
  if (data[2] != kGzipCmDeflate) {
    return false;  // 只支持 deflate
  }

  const uint8_t flg = data[3];
  if ((flg & kFlgReserved) != 0) {
    return false;  // 保留位必须为 0
  }

  std::size_t pos = kGzipFixedHeaderSize;

  if ((flg & kFlgFextra) != 0) {
    if (pos + 2 > size) {
      return false;
    }
    const std::size_t xlen = static_cast<std::size_t>(data[pos]) |
                             (static_cast<std::size_t>(data[pos + 1]) << 8);
    pos += 2 + xlen;
    if (pos > size) {
      return false;
    }
  }

  if ((flg & kFlgFname) != 0) {
    if (!SkipZeroTerminated(data, size, pos)) {
      return false;
    }
  }

  if ((flg & kFlgFcomment) != 0) {
    if (!SkipZeroTerminated(data, size, pos)) {
      return false;
    }
  }

  if ((flg & kFlgFhcrc) != 0) {
    pos += 2;
    if (pos > size) {
      return false;
    }
  }

  offset = pos;
  return true;
}

}  // namespace decompress
