#pragma once

// POSIX 时区串的构造。**纯逻辑**：不依赖 ESP-IDF，可在主机上单测
// （见 tests/host/test_time_zone.cpp，本文件在 run_tests.sh 的 SOURCES 里）。

#include <string>

namespace net {

// 把"本地时间与 UTC 的差"编成 POSIX TZ 串。
//
// 这个函数存在的理由：POSIX 的符号与常识**相反**（UTC+8 要写成 "CST-8"），而且
// 半小时时区必须写成 "hh:mm"。两处写错的后果都是**静默**的 —— 负偏移会生成
// "CST--5" 这类非法串、被 libc 当作 UTC（差几小时）；整数小时截断会让 +5:30
// 差 30 分钟。把它放在有单测的代码里，而不是让人手写字符串。
//
// 契约：utc_offset_seconds 是 60 的整数倍且落在 [-12h, +14h]；std_name ≥ 3 字符。
// 契约由调用方在编译期 static_assert 守住，本函数不重复检查。
std::string PosixTzString(long utc_offset_seconds, const char* std_name);

}  // namespace net
