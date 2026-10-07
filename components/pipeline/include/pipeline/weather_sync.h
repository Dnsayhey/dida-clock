#pragma once

// 天气数据流水线：把 net（HTTP）→ decompress（gzip）→ weather（解析）
// → store（并发安全写入）串成一条链路。
//
// 组件名取 pipeline 而非 sync：`sync()` 是 POSIX 函数，
// `namespace sync` 会与它冲突（命名空间要避开 C 库符号，见 CONVENTIONS §2）。
//
// **并发约定：本类的所有方法都会做阻塞 I/O，只允许由启动任务调用。**
// 它写 store 时用 WeatherStore 的锁保护，UI 任务只通过 Snapshot() 读 ——
// 绕过 store 直接共享数据字段的写法会立刻退回"UI 读到正在被改写的
// std::string"的撕裂问题。

#include <string>

#include "storage/device_config.h"
#include "store/weather_store.h"

namespace pipeline {

struct WeatherApiConfig {
  std::string base_url;
  std::string api_key;

  bool IsConfigured() const { return !base_url.empty() && !api_key.empty(); }
};

class WeatherSync {
 public:
  WeatherSync(store::WeatherStore& store, WeatherApiConfig api_config);

  // 若配置里已有 location_id 就直接采用；否则用城市名查一次并落库。
  // 返回是否最终具备可用的 location_id 与经纬度。
  bool EnsureLocation(const config::DeviceConfig& device_config);

  // 以下三个各自独立，失败只影响对应的数据源状态。
  void SyncCurrentConditions(const std::string& location_id);
  void SyncDailyForecast(const std::string& location_id);
  void SyncAirQuality(const std::string& latitude,
                      const std::string& longitude);

  // 当前可用的 location 信息（EnsureLocation 之后有效）。
  const std::string& LocationId() const { return location_id_; }
  const std::string& Latitude() const { return latitude_; }
  const std::string& Longitude() const { return longitude_; }

 private:
  // 单次请求的通用流程：请求 → 解压 → 解析。
  // 失败时通过 error 返回**分级后的**原因（网络/HTTP/解压/业务码/无数据）。
  bool FetchJson(const std::string& url, std::string& json_out,
                 std::string& error);

  store::WeatherStore& store_;
  WeatherApiConfig api_config_;

  std::string location_id_;
  std::string latitude_;
  std::string longitude_;
};

}  // namespace pipeline
