#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>

namespace net {

// SNTP 时间同步。
//
// 只在启动时配置一次，之后由 lwIP 自己维护周期性同步。
// 反复调用会重置 SNTP 状态、打断已经在跑的同步周期（表现为时间长期不同步），
// 所以 StartSntp 内部用幂等标志保护：可重复调用，只生效一次。
//
// ESP-IDF 6.0 sntp.h 已移除，必须用 esp_sntp.h。

// SNTP 启动参数。
struct SntpConfig {
  // 按顺序尝试的候选服务器（当前这台接收超时、或返回 KoD 包时，lwIP 自动切
  // 到下一个）。空项（nullptr 或空串）会被跳过，便于按行屏蔽某一台。
  //
  // 两条约束不满足时都不会报错，只会静默退化：
  //   * server_count 超过 CONFIG_LWIP_SNTP_MAX_SERVERS 时，超出的部分在
  //     lwIP 的 sntp_setservername() 里被直接忽略（StartSntp 会打警告）；
  //   * servers[i] 指向的字符串必须**长期存活** —— lwIP 存的是指针而非拷贝。
  const char* const* servers = nullptr;
  std::size_t server_count = 0;

  // 本地时区：本地时间与 UTC 的差（秒），中国为 8*3600。
  long utc_offset_seconds = 0;

  // 时区缩写，如 "CST"。只影响 strftime("%Z") 与 tzname[]，界面不显示。
  // POSIX 要求至少 3 个字符。
  const char* tz_name = "UTC";
};

// 启动 SNTP。可重复调用，内部只生效一次（重复调用会重置 SNTP 状态、打断已经
// 在跑的同步周期，表现为时间长期不同步）。
void StartSntp(const SntpConfig& config);

// 等待系统时间被设置成功（即在 2020 年之后）。
// 返回 false 表示超时。
bool WaitForTime(uint32_t timeout_ms);

// 本地时间是否已同步（年份是否已达 2020 之后）。
bool IsTimeSynced();

// 一次读到的本地时钟。三个字段来自**同一次**取样。
//
// 为什么不拆成三个函数分别读：跨秒/跨日的边界上，分开取样会得到互相矛盾的
// 一组（日期已是新的一天，时间还是 23:59:59）；而"秒内位置"只有与日期、时间
// 同源时，才能用来把刷新周期对齐到秒边界。
struct LocalClockReading {
  std::string date;             // ISO "YYYY-MM-DD"；时间未同步时为空
  std::string time;             // "HH:MM:SS"；时间未同步时为空
  uint32_t ms_into_second = 0;  // 当前秒内已过的毫秒 [0,999]
};

// 读当前本地时钟。时间未同步时 date/time 为空串。
LocalClockReading ReadLocalClock();

}  // namespace net
