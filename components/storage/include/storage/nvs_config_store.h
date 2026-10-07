#pragma once

#include <esp_err.h>

#include "storage/device_config.h"

namespace storage {

// NVS 配置读写。
//
// 两点刻意的设计：
//   1. 没有全局单例 + 逐字段 getter/setter，而是一次读出一个完整快照、
//      一次写入一份完整配置 —— 减少 NVS 往返，也让调用方拿到自洽的数据。
//   2. 每次操作自己开关 NVS 句柄，避免长期持有句柄带来的生命周期问题。
class NvsConfigStore {
 public:
  // 打开命名空间（不存在则创建）。
  esp_err_t Begin();

  // 读出全部配置。缺失的键返回空串 / 默认值。
  config::DeviceConfig Load() const;

  // 写入全部配置（已做 Normalize）。
  esp_err_t Save(const config::DeviceConfig& config);

  // 恢复出厂：清空整个命名空间，但不擦除整块 NVS 分区。
  esp_err_t FactoryReset();

  // ---- 单项写入 ----
  //
  // 为什么需要它们：亮度/主题由 UI 任务随时修改，而 Wi-Fi 配置由 net 任务
  // 读取。如果改设置时做"整份配置读-改-写"，就会与 net 任务产生对同一份
  // 配置的跨任务读改写竞争。
  // 只写单个键则没有这个问题：NVS 自身对单键操作是原子的。
  esp_err_t SaveBrightnessMode(int mode);
  esp_err_t SaveThemeMode(int mode);
};

}  // namespace storage
