#pragma once

// 复制为 time_config_local.h 并按需修改。
// time_config_local.h 已被 .gitignore 忽略，不会进入版本历史。
//
// 这个头文件会**整体替换** time_config.h 里的默认值，所以下面三个常量都要
// 定义（名字与类型都要一致）。
//
// 写法要求（见 time_config.h 的两条约束）：
//   * 候选个数不得超过 CONFIG_LWIP_SNTP_MAX_SERVERS（否则多出来的会被 lwIP
//     静默丢弃，代码里的 static_assert 会先把它拦成编译错误）；
//   * 候选只能是字符串字面量，不能是运行期构造出来的临时字符串。

namespace dida {

inline constexpr const char* const kNtpServers[] = {
    "ntp5.aliyun.com",
    "ntp.tencent.com",
    "cn.pool.ntp.org",
};

// 本地时间与 UTC 的差，秒。这里是中国的 +8 小时。
// 改成其它地区时记得同时改 kTzName（例如 UTC+9 → 9*3600 / "JST"）。
inline constexpr long kUtcOffsetSeconds = 8 * 3600;

// 时区缩写，只影响 %Z 与 tzname[]，不参与时间计算。POSIX 要求至少 3 个字符。
inline constexpr const char* const kTzName = "CST";

}  // namespace dida

// 其它地区的例子：
//
//   全球池：   "pool.ntp.org"
//   中国区池： "cn.pool.ntp.org"（该区有数十台活跃服务器）
//   欧洲：     "europe.pool.ntp.org"
//
// 注意 NTP Pool 的使用政策：设备量很大时应当自建/自同步一台本地服务器，
// 再从上游池同步，而不是让每台设备都去直接轮询池。
//
// 时区只能配**固定偏移**（kUtcOffsetSeconds），因此不覆盖夏令时：有 DST 的
// 地区在夏令时期间会差一小时。本项目面向无夏令时的市场；若要支持 DST，
// 需要把这里扩成带起止规则的结构，那是一次单独的设计。
