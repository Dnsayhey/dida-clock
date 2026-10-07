// POSIX 时区串构造的单测（主机运行）。
//
// 为什么这些测试值得存在：时区是"错了也看不出来"的典型 ——
//   * 负偏移写错会得到 "CST--5" 这类**非法串**，而 libc 对无法解析的 TZ
//     静默当作 UTC，时间差几个小时却毫无报错；
//   * 半小时时区（印度 +5:30）用整数小时截断会静默差 30 分钟。
// 两者都不会崩溃、不会打日志，只有盯着表看才发现。
//
// 这里的测试分两层：
//   1. 纯字符串生成（PosixTzString 的输出）；
//   2. **用宿主 libc 反证符号约定** —— setenv/tzset 之后看 localtime_r 的
//      结果。第 2 层才是真正的验证：POSIX 的符号与常识相反这件事，
//      光靠读代码或读文档都可能记反，跑一遍不会。
//
// 局限：这里跑的是宿主 libc（macOS/Linux）的 TZ 解析。设备侧是 newlib，
// 两者都实现 POSIX TZ，但"设备侧行为一致"**不能由本文件证明**。

#include <cstdlib>
#include <ctime>
#include <string>

#include "net/time_zone.h"
#include "test_framework.h"

using net::PosixTzString;

namespace {

// 用给定 TZ 串解释 1970-01-01T00:00:00Z，返回 "YYYY-MM-DD HH:MM"。
//
// 每例都重新 setenv + tzset：TZ 是进程级全局状态，用例之间必须显式隔离，
// 否则某条用例的副作用会污染后面的用例（顺序一变结果就变）。
std::string LocalTimeAtEpoch(const std::string& tz) {
  setenv("TZ", tz.c_str(), 1);
  tzset();
  const std::time_t epoch = 0;
  std::tm local = {};
  localtime_r(&epoch, &local);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
  return std::string(buffer);
}

}  // namespace

// ---------------- 串的生成 ----------------

TEST_CASE(Tz_整小时偏移) {
  CHECK_EQ(PosixTzString(8 * 3600, "CST"), std::string("CST-8"));  // UTC+8
  CHECK_EQ(PosixTzString(-5 * 3600, "CST"), std::string("CST5"));  // UTC-5
  CHECK_EQ(PosixTzString(0, "UTC"), std::string("UTC0"));
  CHECK_EQ(PosixTzString(14 * 3600, "CST"), std::string("CST-14"));
  CHECK_EQ(PosixTzString(-12 * 3600, "CST"), std::string("CST12"));
}

TEST_CASE(Tz_半小时与四十五分偏移) {
  // 印度、尼泊尔、伊朗、部分澳洲 —— 用整数小时截断会静默差 30/45 分钟。
  CHECK_EQ(PosixTzString(5 * 3600 + 30 * 60, "CST"), std::string("CST-5:30"));
  CHECK_EQ(PosixTzString(5 * 3600 + 45 * 60, "CST"), std::string("CST-5:45"));
  CHECK_EQ(PosixTzString(-(5 * 3600 + 30 * 60), "CST"), std::string("CST5:30"));
}

// 子小时偏移最容易写错：小时部分为 0 时，用 posix_seconds / 3600 会把负号
// 截断掉，把 UTC+0:30 写成 UTC-0:30（方向正好相反）。
TEST_CASE(Tz_零小时的子小时偏移仍保留符号) {
  CHECK_EQ(PosixTzString(30 * 60, "CST"), std::string("CST-0:30"));  // UTC+0:30
  CHECK_EQ(PosixTzString(-(30 * 60), "CST"),
           std::string("CST0:30"));  // UTC-0:30
}

// ---------------- 用宿主 libc 反证符号约定 ----------------

TEST_CASE(Tz_东八区经libc解释正确) {
  // 若把 POSIX 的符号搞反，这里会得到前一天 16:00 而不是 08:00。
  CHECK_EQ(LocalTimeAtEpoch(PosixTzString(8 * 3600, "CST")),
           std::string("1970-01-01 08:00"));
}

TEST_CASE(Tz_西五区经libc解释正确) {
  // 负偏移：串是 "CST5"（不带符号），libc 应解释为 UTC-5 —— 前一天 19:00。
  CHECK_EQ(LocalTimeAtEpoch(PosixTzString(-5 * 3600, "CST")),
           std::string("1969-12-31 19:00"));
}

TEST_CASE(Tz_半小时时区经libc解释正确) {
  CHECK_EQ(LocalTimeAtEpoch(PosixTzString(5 * 3600 + 30 * 60, "CST")),
           std::string("1970-01-01 05:30"));
  CHECK_EQ(LocalTimeAtEpoch(PosixTzString(-(5 * 3600 + 30 * 60), "CST")),
           std::string("1969-12-31 18:30"));
}

TEST_CASE(Tz_零小时子小时时区经libc解释正确) {
  // 这两条正是"符号被截断"那个 bug 的探测器：写反了结果会互相交换。
  CHECK_EQ(LocalTimeAtEpoch(PosixTzString(30 * 60, "CST")),
           std::string("1970-01-01 00:30"));
  CHECK_EQ(LocalTimeAtEpoch(PosixTzString(-(30 * 60), "CST")),
           std::string("1969-12-31 23:30"));
}

// ---------------- 反例：证明这类 bug 真实存在 ----------------

// 手工拼 TZ 串（整数小时 + "CST-%ld"）对负偏移会得到 "CST--5"。这条测试把该串
// 喂给 libc，钉住"它被静默当作 UTC"这个事实 —— 否则"为什么必须走
// PosixTzString()"就只剩一句无从验证的说法。（§14.2：检查必须能失败。）
TEST_CASE(Tz_非法串被libc静默当作UTC) {
  CHECK_EQ(LocalTimeAtEpoch("CST--5"), std::string("1970-01-01 00:00"));
}
