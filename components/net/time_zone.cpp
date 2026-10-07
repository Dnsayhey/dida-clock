#include "net/time_zone.h"

#include <cstdio>
#include <cstdlib>

namespace net {

std::string PosixTzString(long utc_offset_seconds, const char* std_name) {
  // POSIX 串里写的是**取反**后的偏移：UTC+8 → "CST-8"。
  const long posix_seconds = -utc_offset_seconds;

  // 时与分都从绝对值算，符号单独处理。
  //
  // 不能直接用 posix_seconds / 3600：C 的整数除法向零截断，"CST-0:30"
  // （UTC+0:30）会被算成小时 0 且符号丢失，生成 "CST0:30" —— 那是 UTC-0:30，
  // 方向正好相反。
  const long abs_seconds = std::labs(posix_seconds);
  const long hours = abs_seconds / 3600;
  const long minutes = (abs_seconds % 3600) / 60;
  // POSIX 里不带符号就是正偏移，所以只有负数需要显式写 '-'。
  const char* sign = posix_seconds < 0 ? "-" : "";

  char buffer[32];
  if (hours == 0 && minutes == 0) {
    std::snprintf(buffer, sizeof(buffer), "%s0", std_name);
  } else if (minutes == 0) {
    std::snprintf(buffer, sizeof(buffer), "%s%s%ld", std_name, sign, hours);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%s%s%ld:%02ld", std_name, sign,
                  hours, minutes);
  }
  return std::string(buffer);
}

}  // namespace net
