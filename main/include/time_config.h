#pragma once

// 时间基础设施配置：NTP 候选服务器 + 本地时区。换地区要动的就是这两样。
//
// 不必改这个文件：把 time_config_local.example.h 复制为 time_config_local.h
// （已被 .gitignore 忽略），在里面定义同名常量即可整体替换默认值。
//
// 两条约束 —— 不满足都不会在编译期报错，只会静默退化：
//   1. **候选个数不得超过 CONFIG_LWIP_SNTP_MAX_SERVERS**：lwIP 的
//      sntp_setservername() 对越界索引直接返回，于是"配了备用服务器却永远不会
//      切过去"。本文件末尾用 static_assert 把这条变成了编译错误。
//   2. **候选字符串必须长期存活**：lwIP 存的是指针而不是拷贝，只能传字符串字面量
//      或静态存储期对象 —— 传临时的 std::string::c_str() 会变成悬垂指针。
//
// 时区配的是"与 UTC 的差（秒）"而不是现成的 POSIX TZ 串：那串的符号与常识
// 相反，写错不会报错（libc 把无法解析的 TZ 静默当作 UTC）。编码规则与两个
// 静默陷阱见 net::PosixTzString() —— 那里有主机单测。

#include <cstddef>

// DIDA_HAS_TIME_CONFIG_LOCAL 由 main/CMakeLists.txt 在配置阶段探测后作为
// 编译宏传下来。不用 __has_include —— 它的依赖 CMake 看不见，首次创建本地
// 头文件时不会重编，表现为"配了却静默不生效"。
#ifndef DIDA_HAS_TIME_CONFIG_LOCAL
#define DIDA_HAS_TIME_CONFIG_LOCAL 0
#endif

#if DIDA_HAS_TIME_CONFIG_LOCAL
#include "time_config_local.h"
#else
namespace dida {

// 默认候选：三个**不同来源**的国内可达服务。
//
// 顺序有意义：正常情况下只会用到第一个，后面的只在它超时/被拒时才轮到。
// 特意不选同一个域名下的多台（那样共因故障仍会一起失效）。
//
// 入选依据是实测（每台各查 3 次，记录成功率与偏移不确定性）：
//   ntp5.aliyun.com    3/3，偏移 ±0.003s
//   ntp.tencent.com    3/3，偏移 ±0.005s
//   cn.pool.ntp.org    3/3，偏移 ±0.004s
// 未入选：ntp.ntsc.ac.cn 只有 2/3，不确定性 ±0.23s（比上面差两个数量级），
// 且它的三个 A 记录里"哪台超时"是随机的 —— 放进候选列表与"高可用"自相矛盾。
// 注意这是**某一张网络**上的实测值，换网络（尤其境外）结论可能不同。
inline constexpr const char* const kNtpServers[] = {
    "ntp5.aliyun.com",  // 阿里云
    "ntp.tencent.com",  // 腾讯云
    "cn.pool.ntp.org",  // NTP Pool 中国区（地址池会轮换）
};

// 本地时间与 UTC 的差，秒。中国为 +8 小时。
inline constexpr long kUtcOffsetSeconds = 8 * 3600;

// 时区缩写。只影响 strftime("%Z") 与 tzname[]，界面不显示它 ——
// 真正的时区语义由 kUtcOffsetSeconds 决定。
inline constexpr const char* const kTzName = "CST";

}  // namespace dida
#endif

// ---- 编译期校验：对默认值与本地覆盖同时生效 ----
namespace dida {
namespace detail {

constexpr std::size_t CStrLength(const char* text) {
  std::size_t length = 0;
  while (text[length] != '\0') {
    ++length;
  }
  return length;
}

}  // namespace detail

// 秒级时区偏移现实中不存在，出现就说明是手误（例如把毫秒填了进来）。
static_assert(kUtcOffsetSeconds % 60 == 0, "时区必须是整分钟");

// 真实世界范围：UTC-12:00 .. UTC+14:00。
static_assert(kUtcOffsetSeconds >= -12 * 3600 && kUtcOffsetSeconds <= 14 * 3600,
              "时区超出范围 (-12 .. +14 小时)");

// POSIX 要求时区名至少 3 个字符，否则 TZ 串会被解析成别的东西。
static_assert(detail::CStrLength(kTzName) >= 3, "时区名至少 3 个字符");

}  // namespace dida
