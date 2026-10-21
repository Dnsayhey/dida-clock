# OTA 更新说明

当前实现：**启动时检查一次 manifest，有新版本就自动更新，失败则跳过并继续启动。**

不含定时检查、用户确认、下载取消；这些是后续可选功能。

## 1. 流程

```text
上电
  -> 读配置
  -> 连 Wi-Fi
  -> SNTP 对时
  -> 请求 manifest（3 秒超时）
       -> 失败 / 版本不新 / 镜像超槽
            -> 跳过，正常启动
       -> 有新版本
            -> 初始化页显示“更新固件 xx%”
            -> 写入 inactive OTA 槽
            -> 失败：记录日志，继续启动旧固件
            -> 成功：重启
  -> 新固件启动
  -> 启动健康后 mark valid
  -> 正常天气流程
```

放在 SNTP 之后是必须的：正式 HTTPS 源需要真实时间校验证书，刚上电的 1970 年时间会让证书校验失败。本地 HTTP 测试不敏感，但走同一条路径。

## 2. 版本号

固件版本来自仓库根目录的 [`version.txt`](../version.txt)，CMake 把它设为 `PROJECT_VER`，进而进入 `esp_app_desc`。

```text
version.txt: 0.1.0
manifest.version: 0.1.0
```

发布时改 `version.txt`，构建，再把 manifest 的 `version` 改成同一个值。

版本比较是严格的 `major.minor.patch`：远端必须**大于**当前版本才更新，相同或更旧都跳过，避免服务器误操作导致降级。

## 3. manifest

```json
{
  "version": "0.1.0",
  "url": "http://192.168.31.171:8000/dida.bin",
  "size": 1572240
}
```

- `version`：语义化版本，必须三段数字。
- `url`：固件地址；本地测试是 HTTP，正式应为 HTTPS。
- `size`：可选；填写时会在下载前检查是否超过 inactive 槽大小。

设备只固定知道 **manifest URL**。以后只换固件文件地址时，改 manifest 里的 `url` 即可，板子不用动；换 manifest 域名才需要改固件配置或重刷。

## 4. 失败与回滚

已实测的失败路径：

| 场景 | 行为 |
|---|---|
| manifest 地址不通 | 打日志，跳过，正常启动旧固件 |
| manifest 404 / JSON 错误 | 打日志，跳过 |
| 固件 URL 404 | `esp_https_ota_begin` 失败，旧固件继续启动 |
| 固件内容不是合法镜像 | 下载/校验失败，旧固件继续启动 |
| 下载中断电/复位 | inactive 槽写入不完整，bootloader 仍启动旧固件 |

回滚：

- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
- 新固件第一次启动处于 pending verify
- 应用在启动流程走到 `kReady` 后调用 `esp_ota_mark_app_valid_cancel_rollback()`
- 如果新固件在 mark valid 之前崩溃/主动复位，bootloader 下次启动回滚到上一版

测试时曾用 0.4.0 坏固件：启动后不 mark valid、主动重启，bootloader 从 `0x200000` 回滚到 `0x10000` 的 0.3.0。当前发布基线已改为 0.1.0。

## 5. 本地测试

本地测试固件需要 `main/include/ota_config_local.h`（已被 `.gitignore` 忽略）：

```cpp
#define DIDA_OTA_MANIFEST_URL "http://192.168.31.171:8000/manifest.json"
#define DIDA_OTA_BUILD_LABEL "target"
```

测试服务器目录只需要：

```text
manifest.json
dida.bin
```

启动：

```bash
python3 -m http.server 8000 --bind 0.0.0.0 --directory /tmp/ota_server
```

测试目标固件与源固件要使用不同版本；源固件烧到板子，目标固件放在服务器。升级完成后的目标固件再检查 manifest，版本相同就跳过，不会循环。

本地 HTTP 需要在 `sdkconfig` 里临时打开：

```ini
CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y
```

**这只用于局域网测试，正式固件必须关掉。**

## 6. 正式上线前还缺什么

当前 manifest URL 只存在于被 git 忽略的 `ota_config_local.h`。正常构建没有这个头时，`RunOtaUpdateIfConfigured()` 直接返回，**OTA 不启用**。

正式上线前必须补：

1. 一个可提交的默认 manifest URL，例如：
   ```cpp
   #define DIDA_OTA_MANIFEST_URL "https://ota.example.com/dida/manifest.json"
   ```
   本地测试再用 `ota_config_local.h` 覆盖；或把 URL 放进 NVS 做运行时配置。
2. 正式构建关闭 `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP`。
3. 服务端使用 HTTPS；代码已给 manifest 和 OTA 都挂了证书 bundle。
4. 用正式域名做一次真实 HTTPS 升级测试。

## 7. 分区与体积

当前：

```text
ota_0  0x10000 .. 0x200000  0x1F0000
ota_1  0x200000 .. 0x3F0000 0x1F0000
末尾   0x3F0000 .. 0x400000 64KB 未使用
```

槽大小必须 64KB 对齐，两个等大槽最大只能到 `0x1F0000`。当前固件约 1.5 MiB，OTA 槽约 1.94 MiB。分区表一旦有设备在跑 OTA 就不能再通过 OTA 修改，只能 USB 重刷。

## 8. 版本号显示

恢复出厂页在“Wi-Fi 与位置都会丢失”下面显示 `v0.1.0`。版本由 `esp_app_desc` 读取，经 `BuildFirmwareVersion()` 注入页面，页面层不依赖 ESP-IDF。
