#pragma once

namespace dida {

// 启动阶段检查 manifest，并在有新版本时执行 OTA。
//
// 只在编译时提供了 ota_config_local.h、且 DIDA_OTA_MANIFEST_URL 非空时
// 才真正请求；否则立即返回。
void RunOtaUpdateIfConfigured();

// 如果本次启动来自 OTA（bootloader 处于 pending verify 状态），标记为有效。
// 必须在启动流程确认健康后调用；否则 bootloader 会在下次复位时回滚。
void MarkOtaAppValidIfNeeded();

}  // namespace dida
