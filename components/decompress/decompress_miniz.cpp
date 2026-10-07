// 设备侧的 inflate 实现：调用 ESP32-C3 ROM 里的 miniz（tinfl_*）。
//
// 用 ROM 版本的好处是**零 Flash 开销** —— ROM 已经带了 tinfl 的全部代码。
// 代价是 ROM 只导出 tinfl_* 这一层，没有 mz_uncompress / mz_inflateInit2，
// 所以 gzip 容器要由我们自己剥掉头部（见 decompress_format.cpp）。
//
// 本文件只在固件构建中参与编译；主机单测只编译 decompress_format.cpp。

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "decompress/decompress.h"
#include "miniz.h"

namespace decompress {
namespace {

// 解压输出的上限。
//
// 真实响应最大约 3.7 KB（7 日预报），64 KB 留了充足余量，同时把
// "解压炸弹"（极小的压缩数据膨胀成极大输出）挡在堆耗尽之前。
// 注意：tinfl_decompress_mem_to_heap() 那种"一次解到堆"的接口没有上限，
// 不要换回那种写法。
constexpr std::size_t kMaxOutputBytes = 64 * 1024;

// 初始输出缓冲。8 KB 足以一次装下所有真实响应，正常路径不会触发扩容。
constexpr std::size_t kInitialOutputBytes = 8 * 1024;

}  // namespace

bool Inflate(const uint8_t* data, std::size_t size, std::string& out,
             std::string& error) {
  out.clear();
  error.clear();

  if (data == nullptr || size == 0) {
    error = "响应体为空";
    return false;
  }

  const Format format = Detect(data, size);

  // 未压缩：原样返回，交给上层 JSON 解析
  if (format == Format::kPlain) {
    out.assign(reinterpret_cast<const char*>(data), size);
    return true;
  }

  const uint8_t* deflate_src = data;
  std::size_t deflate_len = size;
  int flags = 0;

  if (format == Format::kGzip) {
    std::size_t offset = 0;
    if (!GzipDeflateOffset(data, size, offset)) {
      error = "gzip 头解析失败（头部不完整或格式非法）";
      return false;
    }
    deflate_src = data + offset;
    deflate_len = size - offset;
    // 可选字段已被跳过，剩下的是裸 deflate；尾部的 CRC32/ISIZE 由 tinfl
    // 在遇到 deflate 流结束时自然忽略。
    flags = 0;
  } else if (format == Format::kZlib) {
    flags = TINFL_FLAG_PARSE_ZLIB_HEADER;
  } else {
    error = std::string("未知压缩格式: ") + FormatName(format);
    return false;
  }

  // tinfl_decompressor 有 11000 字节（实测）。
  //
  // ROM 的便利接口 tinfl_decompress_mem_to_heap() 会把它放在**栈上**。
  // 这条路径实测直接压爆了 main 任务（栈仅 3584 字节，panic 为
  // Stack protection fault），而 net 任务（12288 字节）也只余约 1.5 KB，
  // 真同步时同样危险。
  //
  // 所以这里改用低层 tinfl_decompress()，把状态显式放到**堆**上：
  // 栈占用从约 11.5 KB 降到几百字节，两个任务都变得宽裕。
  void* raw = std::malloc(sizeof(tinfl_decompressor));
  if (raw == nullptr) {
    error = "解压状态分配失败（堆不足）";
    return false;
  }
  auto* decomp = static_cast<tinfl_decompressor*>(raw);
  tinfl_init(decomp);

  std::vector<uint8_t> buffer(kInitialOutputBytes);
  std::size_t produced = 0;
  const uint8_t* in_next = deflate_src;
  std::size_t in_remaining = deflate_len;

  bool ok = false;
  std::string failure;

  for (;;) {
    std::size_t in_bytes = in_remaining;
    std::size_t out_bytes = buffer.size() - produced;

    const tinfl_status status =
        tinfl_decompress(decomp, in_next, &in_bytes, buffer.data(),
                         buffer.data() + produced, &out_bytes,
                         static_cast<mz_uint32>(
                             flags | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF));

    in_next += in_bytes;
    in_remaining -= in_bytes;
    produced += out_bytes;

    if (status == TINFL_STATUS_DONE) {
      ok = true;
      break;
    }
    if (status == TINFL_STATUS_HAS_MORE_OUTPUT) {
      if (buffer.size() >= kMaxOutputBytes) {
        failure = "解压输出超过上限";
        break;
      }
      // 非环绕模式下解压器靠"输出起点到当前位置"的偏移做回溯，
      // 该位置以偏移而非指针保存，所以扩容搬移缓冲区是安全的
      // （旧数据必须保留）。
      buffer.resize(std::min(buffer.size() * 2, kMaxOutputBytes));
      continue;
    }
    if (status == TINFL_STATUS_NEEDS_MORE_INPUT) {
      failure = "压缩数据不完整";
      break;
    }
    if (status == TINFL_STATUS_ADLER32_MISMATCH) {
      failure = "zlib 校验和不匹配";
      break;
    }
    failure = "inflate 失败";
    break;
  }

  std::free(raw);

  if (!ok) {
    error = failure + "（输入格式 " + FormatName(format) + "）";
    return false;
  }

  out.assign(reinterpret_cast<const char*>(buffer.data()), produced);
  return true;
}

}  // namespace decompress
