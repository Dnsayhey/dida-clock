#pragma once

// WeatherStore —— 天气数据的并发安全容器。
//
// 要解决的问题：天气数据在 net 任务里被改写（std::string 的堆缓冲释放/重分配），
// 而 UI 任务按值拷贝同一批字段。不加锁时 UI 可能拷到正在被释放的指针 →
// use-after-free → 堆损坏 → 概率性崩溃/花屏；直接返回内部指针则随时失效。
//
// 做法：
//   1. 单一 std::mutex 守护全部字段；
//   2. 读侧只提供 Snapshot()，**返回完整拷贝**，绝不暴露内部指针/引用；
//   3. 写侧每次更新都在锁内完成一个逻辑单元的替换；
//   4. 读到拷贝后在锁外做格式化与渲染 —— 锁内绝不调用 LVGL，
//      避免"数据锁 → LVGL 锁"的嵌套顺序问题。
//
// 纯逻辑（时间戳由调用方传入）：可在主机上单测，含 ThreadSanitizer 的无竞争验证。

#include <cstdint>
#include <mutex>
#include <string>

#include "weather/weather_types.h"

namespace store {

// 一份完整、自洽的天气数据快照。纯值语义：持有它不需要任何锁。
struct WeatherSnapshot {
  weather::WeatherNow now;
  weather::AirQualityNow air_quality;
  weather::DailyForecast daily{};
  weather::CityLookup city;

  weather::SyncState now_state;
  weather::SyncState air_quality_state;
  weather::SyncState daily_state;
  weather::SyncState city_state;

  // 每次写入（数据或状态）自增。读取端可据此跳过无变化的重绘。
  uint32_t sequence = 0;
};

class WeatherStore {
 public:
  WeatherStore() = default;
  WeatherStore(const WeatherStore&) = delete;
  WeatherStore& operator=(const WeatherStore&) = delete;

  // ---------------- 写入侧（只应由 net 任务调用）----------------

  void SetCurrentConditions(const weather::WeatherNow& now);
  void SetAirQuality(const weather::AirQualityNow& air_quality);
  void SetDailyForecast(const weather::DailyForecast& daily);
  void SetCityLookup(const weather::CityLookup& city);

  void MarkSyncing(weather::DataSource source);
  void MarkReady(weather::DataSource source, uint64_t now_ms);
  void MarkFailed(weather::DataSource source, std::string message,
                  uint64_t now_ms);

  // ---------------- 读取侧（UI 任务调用）----------------

  // 返回一份拷贝。调用方随后在**锁外**自由使用，不再持有任何内部引用。
  WeatherSnapshot Snapshot() const;

  // 轻量的序号查询，用于"是否有新数据"的快速判断。
  uint32_t Sequence() const;

 private:
  // 以下都要求已持有 mutex_。
  weather::SyncState& StateLocked(weather::DataSource source);
  void BumpSequenceLocked();

  mutable std::mutex mutex_;
  WeatherSnapshot data_;
};

}  // namespace store
