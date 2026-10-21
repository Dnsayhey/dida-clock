#pragma once

// 复制为 ota_config_local.h 后使用。该文件被 .gitignore 忽略。
//
// 本地测试流程：
//   1. 编译一个"目标固件"（LABEL 写 target；URL 可留空，或指向同一 manifest
//      以验证"版本相同则跳过"），把 build/dida.bin 复制到 HTTP 服务目录；
//   2. 把下面的 URL 指向 manifest，LABEL 写 source，再编译并烧录"源固件"；
//   3. 源固件启动后下载目标固件、重启；目标固件看到同版本会跳过。

#define DIDA_OTA_MANIFEST_URL "http://192.168.31.100:8000/manifest.json"
#define DIDA_OTA_BUILD_LABEL "source"
