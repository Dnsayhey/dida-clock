#pragma once

#include "storage/device_config.h"

namespace dev_config {

// 是否编入了开发默认配置。
// 条件：CONFIG_DIDA_DEV_MODE=y **且** 存在 device_config_local.h。
// 两个条件都要满足 —— 没有那个头文件时绝不注入任何东西。
bool HasDefaults();

// 把开发默认值**逐字段填补**到 config 中空缺的字段上。
//
// 逐字段填补规则：
//   * 字段已有值 -> 保留，只打日志
//   * 默认值为空 -> 跳过
//   * 字段为空且默认值非空 -> 填入
// 因此配过一次网之后，开发默认值不会再插手。
//
// 返回是否改动了至少一个字段（调用方据此决定要不要写回 NVS）。
bool ApplyDefaults(config::DeviceConfig& config);

}  // namespace dev_config
