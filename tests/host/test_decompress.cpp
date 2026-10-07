// decompress 组件的纯逻辑单测（主机运行）。
//
// 重点不是"造几个假头试一下"，而是**用真实抓取的和风天气 gzip 响应**验证：
//   1. 格式识别正确；
//   2. 我算出的 deflate 起始偏移确实能让一个正确的 inflate 还原出原始 JSON。
// 第 2 点用系统 zlib 作为参考实现 —— 这样就把"头部解析是否正确"这件事
// 与"设备端 ROM miniz 是否好用"解耦开，前者能被确定性地验证。

#include <zlib.h>

#include <string>
#include <vector>

#include "decompress/decompress.h"
#include "fixture_loader.h"
#include "test_framework.h"

using decompress::Detect;
using decompress::Format;
using decompress::GzipDeflateOffset;

namespace {

// 用系统 zlib 做"裸 deflate"解压（windowBits = -15），作为参考实现。
bool ReferenceInflateRaw(const uint8_t* data, std::size_t size,
                         std::string& out) {
  z_stream stream{};
  if (inflateInit2(&stream, -15) != Z_OK) {
    return false;
  }
  stream.next_in = const_cast<Bytef*>(data);
  stream.avail_in = static_cast<uInt>(size);

  std::vector<char> buffer(64 * 1024);
  stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
  stream.avail_out = static_cast<uInt>(buffer.size());

  const int rc = inflate(&stream, Z_FINISH);
  const std::size_t produced = buffer.size() - stream.avail_out;
  inflateEnd(&stream);

  if (rc != Z_STREAM_END) {
    return false;
  }
  out.assign(buffer.data(), produced);
  return true;
}

}  // namespace

// ---------------- 格式识别 ----------------

TEST_CASE(Detect_空输入为Unknown) {
  CHECK(Detect(nullptr, 0) == Format::kUnknown);
}

TEST_CASE(Detect_gzip魔数) {
  const std::vector<uint8_t> data{0x1F, 0x8B, 0x08, 0x00};
  CHECK(Detect(data.data(), data.size()) == Format::kGzip);
}

TEST_CASE(Detect_zlib头) {
  // 0x78 0x9C 是常见的 zlib 头：CM=8, CINFO=7, 校验 (0x78<<8|0x9C)%31==0
  const std::vector<uint8_t> data{0x78, 0x9C, 0x00};
  CHECK(Detect(data.data(), data.size()) == Format::kZlib);
}

TEST_CASE(Detect_JSON文本判为未压缩) {
  const std::string json = R"({"code":"200"})";
  CHECK(Detect(reinterpret_cast<const uint8_t*>(json.data()), json.size()) ==
        Format::kPlain);
}

// '{' = 0x7B，其低 4 位为 0xB != 8，不会被误判成 zlib —— 这条专门守住这个边界
TEST_CASE(Detect_左花括号不会被误判为zlib) {
  const std::vector<uint8_t> data{0x7B, 0x00};
  CHECK(Detect(data.data(), data.size()) == Format::kPlain);
}

// ---------------- gzip 头偏移 ----------------

TEST_CASE(GzipOffset_无可选字段为10) {
  const std::vector<uint8_t> data{0x1F, 0x8B, 0x08, 0x00, 0,    0,
                                  0,    0,    0,    0xFF, 0xAA, 0xBB};
  std::size_t offset = 0;
  CHECK(GzipDeflateOffset(data.data(), data.size(), offset));
  CHECK_EQ(offset, 10u);
}

TEST_CASE(GzipOffset_带FNAME) {
  // FLG=0x08 (FNAME)，文件名 "a.b" + NUL
  std::vector<uint8_t> data{0x1F, 0x8B, 0x08, 0x08, 0, 0, 0, 0, 0, 0xFF};
  data.push_back('a');
  data.push_back('.');
  data.push_back('b');
  data.push_back(0x00);
  data.push_back(0xAA);  // deflate 数据的第一个字节

  std::size_t offset = 0;
  CHECK(GzipDeflateOffset(data.data(), data.size(), offset));
  CHECK_EQ(offset, 14u);  // 10 + 4
}

TEST_CASE(GzipOffset_带FEXTRA) {
  // FLG=0x04 (FEXTRA)，XLEN=3 小端，后跟 3 字节
  std::vector<uint8_t> data{0x1F, 0x8B, 0x08, 0x04, 0,    0,    0,   0,
                            0,    0xFF, 0x03, 0x00, 0x11, 0x22, 0x33};
  data.push_back(0xAA);

  std::size_t offset = 0;
  CHECK(GzipDeflateOffset(data.data(), data.size(), offset));
  CHECK_EQ(offset, 15u);  // 10 + 2(XLEN) + 3
}

TEST_CASE(GzipOffset_带FCOMMENT与FHCRC组合) {
  // FLG = FCOMMENT(0x10) | FHCRC(0x02)
  std::vector<uint8_t> data{0x1F, 0x8B, 0x08, 0x12, 0, 0, 0, 0, 0, 0xFF};
  data.push_back('h');  // comment
  data.push_back('i');
  data.push_back(0x00);
  data.push_back(0x12);  // FHCRC 两字节
  data.push_back(0x34);

  std::size_t offset = 0;
  CHECK(GzipDeflateOffset(data.data(), data.size(), offset));
  CHECK_EQ(offset, 15u);  // 10 + 3(comment+ NUL) + 2(FHCRC) = 15
}

TEST_CASE(GzipOffset_头部过短) {
  const std::vector<uint8_t> data{0x1F, 0x8B, 0x08, 0x00};
  std::size_t offset = 0;
  CHECK(!GzipDeflateOffset(data.data(), data.size(), offset));
}

TEST_CASE(GzipOffset_魔数不对) {
  const std::vector<uint8_t> data{0x00, 0x00, 0x08, 0x00, 0, 0, 0, 0, 0, 0xFF};
  std::size_t offset = 0;
  CHECK(!GzipDeflateOffset(data.data(), data.size(), offset));
}

TEST_CASE(GzipOffset_压缩方法非deflate) {
  // CM = 7（非 deflate）
  const std::vector<uint8_t> data{0x1F, 0x8B, 0x07, 0x00, 0, 0, 0, 0, 0, 0xFF};
  std::size_t offset = 0;
  CHECK(!GzipDeflateOffset(data.data(), data.size(), offset));
}

TEST_CASE(GzipOffset_保留位被置位) {
  // FLG = 0x20，属保留位
  const std::vector<uint8_t> data{0x1F, 0x8B, 0x08, 0x20, 0, 0, 0, 0, 0, 0xFF};
  std::size_t offset = 0;
  CHECK(!GzipDeflateOffset(data.data(), data.size(), offset));
}

TEST_CASE(GzipOffset_FNAME没有结尾NUL) {
  std::vector<uint8_t> data{0x1F, 0x8B, 0x08, 0x08, 0, 0, 0, 0, 0, 0xFF};
  data.push_back('a');  // 没有 NUL 结尾
  std::size_t offset = 0;
  CHECK(!GzipDeflateOffset(data.data(), data.size(), offset));
}

TEST_CASE(GzipOffset_FEXTRA声明的长度超出实际数据) {
  // XLEN=0x00FF 但后面只有 2 字节
  std::vector<uint8_t> data{0x1F, 0x8B, 0x08, 0x04, 0,    0,    0,
                            0,    0,    0xFF, 0xFF, 0x00, 0x11, 0x22};
  std::size_t offset = 0;
  CHECK(!GzipDeflateOffset(data.data(), data.size(), offset));
}

// ---------------- 用真实响应做端到端偏移验证 ----------------

// 对每个真实 gzip fixture：用本组件算出偏移，再用系统 zlib 裸 deflate 解压，
// 结果必须与同一份响应的明文 fixture 完全一致。
void CheckRealFixture(const char* gz_name, const char* plain_name) {
  const std::string gz = fixtures::Load(gz_name);
  const std::string plain = fixtures::Load(plain_name);
  if (gz.empty() || plain.empty()) {
    ::test::ReportFailure(__FILE__, __LINE__,
                          std::string("fixture 读取失败: ") + gz_name);
    return;
  }

  const auto* bytes = reinterpret_cast<const uint8_t*>(gz.data());
  CHECK(Detect(bytes, gz.size()) == Format::kGzip);

  std::size_t offset = 0;
  CHECK(GzipDeflateOffset(bytes, gz.size(), offset));

  std::string inflated;
  const bool ok =
      ReferenceInflateRaw(bytes + offset, gz.size() - offset, inflated);
  CHECK(ok);
  CHECK_EQ(inflated, plain);
}

TEST_CASE(真实响应_城市查询_gzip偏移可解出原JSON) {
  CheckRealFixture("city_lookup.json.gz", "city_lookup.json");
}

TEST_CASE(真实响应_实时天气_gzip偏移可解出原JSON) {
  CheckRealFixture("weather_now.json.gz", "weather_now.json");
}

TEST_CASE(真实响应_七日预报_gzip偏移可解出原JSON) {
  CheckRealFixture("daily_forecast.json.gz", "daily_forecast.json");
}

TEST_CASE(真实响应_空气质量_gzip偏移可解出原JSON) {
  CheckRealFixture("air_quality.json.gz", "air_quality.json");
}
