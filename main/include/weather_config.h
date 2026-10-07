#pragma once

// 天气 API 配置。
//
// 真实密钥放在**被 git 忽略**的 weather_config_local.h 里，
// 仓库中只提交 example。这样密钥不会进入版本历史。
//
// 准备方式：
//   cp weather_config_local.example.h weather_config_local.h
//   然后把 BASE_URL 与 API_KEY 填进去。
//
// 任何**被提交**的文件里都不许出现明文密钥（构建脚本、示例、文档里同样不行）。

// DIDA_HAS_WEATHER_CONFIG_LOCAL 由 main/CMakeLists.txt 在配置阶段探测后
// 作为编译宏传下来。不用 __has_include —— 它的依赖 CMake 看不见，
// 首次创建 weather_config_local.h 时不会重编，密钥会静默不生效。
#ifndef DIDA_HAS_WEATHER_CONFIG_LOCAL
#define DIDA_HAS_WEATHER_CONFIG_LOCAL 0
#endif

#if DIDA_HAS_WEATHER_CONFIG_LOCAL
#include "weather_config_local.h"
#endif

// 未提供本地配置时留空。固件仍可编译，天气同步会明确报"未配置"。
#ifndef QWEATHER_API_BASE_URL
#define QWEATHER_API_BASE_URL ""
#endif

#ifndef QWEATHER_API_KEY
#define QWEATHER_API_KEY ""
#endif
