#pragma once

// OTA 本地测试配置。
//
// 真实 URL 放在被 git 忽略的 ota_config_local.h 里，仓库只提交 example。
// 没有本地配置时，ota_update.cpp 的 OTA 检查编译成空实现，不影响正式固件。
//
// DIDA_HAS_OTA_CONFIG_LOCAL 由 main/CMakeLists.txt 在配置阶段探测后作为
// 编译宏传下来（理由同 weather_config.h：__has_include 的依赖 CMake 看不见）。

#ifndef DIDA_HAS_OTA_CONFIG_LOCAL
#define DIDA_HAS_OTA_CONFIG_LOCAL 0
#endif

#if DIDA_HAS_OTA_CONFIG_LOCAL
#include "ota_config_local.h"
#endif

// 空串 = 本次启动不检查 OTA。
#ifndef DIDA_OTA_MANIFEST_URL
#define DIDA_OTA_MANIFEST_URL ""
#endif

// 串口日志里用来区分"源固件"和"OTA 目标固件"。
#ifndef DIDA_OTA_BUILD_LABEL
#define DIDA_OTA_BUILD_LABEL "production"
#endif
