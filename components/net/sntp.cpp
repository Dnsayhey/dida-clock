#include "net/sntp.h"

#include <esp_log.h>
// ESP-IDF 6.0 sntp.h 已移除，必须用 esp_sntp.h
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "net/time_zone.h"
#include "sdkconfig.h"

namespace net {
namespace {

constexpr char kTag[] = "sntp";

bool g_started = false;

// 判断给定时刻是否算"已同步"：把"时间在 2020 年之后"当作已同步。
// 未同步时系统时间是 1970 年，这个判据足够可靠且不依赖内部状态。
constexpr int kSyncedYearThreshold = 2020;

bool IsSyncedTime(std::time_t now) {
  if (now <= 0) {
    return false;
  }
  std::tm local = {};
  localtime_r(&now, &local);
  return (local.tm_year + 1900) >= kSyncedYearThreshold;
}

bool HasValidTime() { return IsSyncedTime(std::time(nullptr)); }

}  // namespace

void StartSntp(const SntpConfig& config) {
  if (g_started) {
    return;  // 只调用一次，之后由 lwIP 自行维护周期同步
  }
  g_started = true;

  // 设置时区。POSIX TZ 的符号与常识相反、且半小时时区必须带分钟 ——
  // 这两处编码规则都在 net::PosixTzString() 里，那里有主机单测。
  // setenv 会**拷贝**字符串，所以这里用临时 std::string 是安全的
  // （与下面 lwIP 保存服务器名指针的情况不同）。
  const std::string tz =
      PosixTzString(config.utc_offset_seconds, config.tz_name);
  setenv("TZ", tz.c_str(), 1);
  tzset();

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);

  // 只注册 lwIP 真正有的槽位：sntp_setservername() 对越界索引**静默返回**，
  // 所以这里必须自己裁剪，否则日志会声称注册了 N 台而实际只有前几台生效。
  constexpr std::size_t kMaxServers = CONFIG_LWIP_SNTP_MAX_SERVERS;
  if (config.server_count > kMaxServers) {
    ESP_LOGW(kTag, "NTP 候选 %u 台，超过 lwIP 槽位 %u：多出的会被忽略",
             static_cast<unsigned>(config.server_count),
             static_cast<unsigned>(kMaxServers));
  }

  const char* first = nullptr;
  std::size_t registered = 0;
  for (std::size_t i = 0;
       config.servers != nullptr && i < config.server_count && i < kMaxServers;
       ++i) {
    if (config.servers[i] == nullptr || config.servers[i][0] == '\0') {
      continue;  // 允许用空项屏蔽某一台，而不用改动后面的次序
    }
    esp_sntp_setservername(static_cast<uint8_t>(i), config.servers[i]);
    if (first == nullptr) {
      first = config.servers[i];
    }
    ++registered;
  }

  if (registered == 0) {
    // 一台都没配成：任何时间同步都好过完全没有（没有时间，界面上的日期与
    // 时间会一直是空白）。这条路径只在列表被配空时才会走到。
    esp_sntp_setservername(0, "pool.ntp.org");
    first = "pool.ntp.org";
    ESP_LOGW(kTag, "NTP 候选列表为空，退回 pool.ntp.org");
  }

  esp_sntp_init();

  ESP_LOGI(kTag, "SNTP 已启动，%u 台候选（首选 %s），时区 %s，重对间隔 %d ms",
           static_cast<unsigned>(registered), first, tz.c_str(),
           CONFIG_LWIP_SNTP_UPDATE_DELAY);
}

bool IsTimeSynced() { return HasValidTime(); }

bool WaitForTime(uint32_t timeout_ms) {
  constexpr uint32_t kPollIntervalMs = 100;
  uint32_t waited = 0;

  while (waited < timeout_ms) {
    if (HasValidTime()) {
      if (waited > 0) {
        ESP_LOGI(kTag, "时间已同步（等待 %u ms）",
                 static_cast<unsigned>(waited));
      }
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
    waited += kPollIntervalMs;
  }

  ESP_LOGW(kTag, "等待时间同步超时（%u ms）",
           static_cast<unsigned>(timeout_ms));
  return false;
}

LocalClockReading ReadLocalClock() {
  LocalClockReading reading;

  // 只取一次时间：日期、时间、秒内位置三者都从这一次取样推出来。
  // 分别取样会在秒边界上给出自相矛盾的一组，而秒内位置正是页面用来
  // 对齐刷新周期的依据，不能与它所描述的那一秒错开。
  struct timeval tv = {};
  if (gettimeofday(&tv, nullptr) != 0 || !IsSyncedTime(tv.tv_sec)) {
    return reading;
  }

  std::tm local = {};
  localtime_r(&tv.tv_sec, &local);

  char date_buffer[16];
  char time_buffer[16];
  std::strftime(date_buffer, sizeof(date_buffer), "%Y-%m-%d", &local);
  std::strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", &local);

  reading.date = date_buffer;
  reading.time = time_buffer;
  reading.ms_into_second = static_cast<uint32_t>(tv.tv_usec / 1000);
  return reading;
}

}  // namespace net
