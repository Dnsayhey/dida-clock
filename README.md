# DIDA (ESP-IDF)

基于 ESP-IDF v6.1 的桌面气象时钟：ESP32-C3 + AirM2M Core + 240x320 ST7789，
无触摸、单实体按键。界面**字体优先**（天气图标用 40x40 A4 掩码 + recolor），
浅色 / 深色 / 自动三档主题（自动跟随日出日落）；
网络请求与解析全在独立任务里，渲染循环不做任何阻塞调用。

| 文档 | 内容 |
|---|---|
| [CONVENTIONS.md](CONVENTIONS.md) | 代码规范与架构约定（分层、线程契约、注释、测试、界面、定时器）—— **改代码前先读** |
| [tests/host/README.md](tests/host/README.md) | 主机单测的方法学与实测结果（含并发自证程序） |
| [docs/硬件说明.md](docs/硬件说明.md) | 板子、屏幕、按键、光敏、背光改造的硬件事实与接线 |
| [docs/和风天气接口实测.md](docs/和风天气接口实测.md) | 和风天气各端点的真实请求与响应样本 |
| [docs/界面原型渲染流程.md](docs/界面原型渲染流程.md) | 改界面前先画 HTML 原型、渲染成图再动代码 |
| [docs/OTA更新.md](docs/OTA更新.md) | OTA 流程、manifest、版本、失败/回滚与上线前检查 |

---

## 功能

### 页面

| 页面 | 内容 |
|---|---|
| **启动** | 品牌（`DIDA`）+ 真实的启动阶段清单（读取配置 → 连接网络 → 同步时间 → 同步天气） |
| **实时天气** | 日期、秒级时钟、滚动信息条（风向/体感/能见度/湿度）、天气图标 + 官方完整名、温度、地名（区县）、AQI 徽章、太空人动画 |
| **7 日预报** | 日期 / 星期 / 天气 / 最低最高温 四列 |
| **设置** | 亮度档位（自动/低/中/高）、主题（浅色/深色/自动） |
| **恢复出厂** | 长按执行（Wi-Fi 与位置配置全部清除）；显示固件版本号 |
| **配网** | SoftAP + 强制门户，中文页面；配网热点为 `DIDA-XXXX` |

启动页与配网页是**闸门页**，不响应任何按键 —— 没配网时按键跳到天气页只会看到空白，
而配网页是强制门户的落地页，跳过它设备就没有任何可用功能。

### 按键

| 手势 | 天气页 | 设置页 | 恢复出厂页 |
|---|---|---|---|
| 单击 | 实时天气 ↔ 7 日预报 | 切换亮度档位 | — |
| 双击 | → 设置页 | → 恢复出厂页 | → 实时天气页 |
| 长按 | 无操作 | 切换主题（浅色 → 深色 → 自动） | 执行恢复出厂 |

---

## 硬件与引脚

以 [`components/board/include/board/board_config.h`](components/board/include/board/board_config.h)
为准（改板子时先改那里）。完整的屏幕 FPC 定义、背光 NMOS 改造和板型差异见
[docs/硬件说明.md](docs/硬件说明.md)。

| 功能 | GPIO |
|---|---|
| TFT_SCLK | 2 |
| TFT_MOSI | 3 |
| TFT_DC | 4 |
| TFT_RST | 5 |
| TFT_CS | 7 |
| 背光 PWM | 6 |
| 光敏传感器 | 0 |
| 按键 | 8 |
| 状态灯 | 12 |

---

## 构建与烧录

```bash
source ~/esp/idf61.sh                   # 本机封装：前置 3.12 解释器再 source export.sh
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

- 首次构建会联网拉取 `managed_components/`（LVGL、cJSON 等），它们不进版本库。
- 固件版本在 `version.txt`，发布时修改它；manifest 的 `version` 必须一致。
- 更换 `partitions.csv` 后必须**全量**烧录 `idf.py flash -a` —— IDF 6.1 的
  `idf.py flash` 默认是增量快速烧录。

### 可选本地配置

下面四个头文件都被 git 忽略，各有一个 `.example.h` 模板。**不提供也能编译**，
只是对应功能不可用：

| 文件 | 作用 | 不提供时 |
|---|---|---|
| `main/include/device_config_local.h` | 开发默认 Wi-Fi / 位置，跳过配网 | 走强制门户配网 |
| `main/include/weather_config_local.h` | 和风天气的 API Host 与 Key | 天气同步报"未配置"，界面给出提示 |
| `main/include/time_config_local.h` | NTP 候选服务器 + 时区（默认中国） | 用 `time_config.h` 里的默认值 |
| `main/include/ota_config_local.h` | OTA manifest 地址（本地测试 / 临时配置） | OTA 检查不启用 |

### 目录与分层

运行时只有**两个任务**：LVGL 任务（渲染、切页、按键轮询 —— **禁止任何阻塞调用**）
与 StartupTask（全部阻塞 I/O：NVS、WiFi、NTP、HTTP、解压）。跨任务共享只允许快照。
契约见 CONVENTIONS §4 与 §5。

```
board ── display ── assets
   │         │
   └── backlight / input / storage / net / weather / decompress
                              │
                        store / scheduler
                              │
                          app / pages
                              │
                        pipeline / portal
                              │
                            main（装配层）
```

分层由 **`PRIV_REQUIRES` 强制** —— 例如 `pages` 里 `#include "net/..."` 会直接
编译失败。`main/` 是唯一的装配层，平台初始化（`esp_wifi_init()`、NVS）放在这里，
组件不假设调用顺序。

文件树、命名规则、以及"为什么这样拆"，见 CONVENTIONS §1 与 §3。

---

## 测试与验证

```bash
./scripts/check_conventions.sh          # 约定（分层/命名/格式/配色/字库覆盖）
python3 scripts/measure_text.py --all   # 固定宽度槽位是否都放得下
cd tests/host && ./run_tests.sh --tsan  # 主机单测 + ThreadSanitizer，不需要开发板
idf.py build                            # 固件构建
```

四项全绿才算完成。**但"全绿"不等于"能跑"**：缺陷往往是"代码写对了，而那条路径
从未被执行过"。完整的交付清单与判断方法见 CONVENTIONS §16。

改了界面文案或字集必须**重新生成字库**（`python3 scripts/gen_fonts.py`），否则缺字
在屏幕上就是空白；天气图标改动后用 `python3 scripts/gen_weather_icons.py` 重新生成；
改版面或配色先用 `./scripts/render_mock.sh` 出图看效果，不要凭文字判断间距。

---

## OTA 更新

启动时（SNTP 之后）检查 manifest，有新版本就自动写入 inactive 槽并重启；
manifest/固件请求失败、版本不新、镜像不合法都会跳过并继续启动旧固件。
bootloader 回滚已开启，新固件在启动健康后才会 mark valid。

manifest、版本规则、失败矩阵、本地测试与上线前检查见
[docs/OTA更新.md](docs/OTA更新.md)。

当前 `ota_config_local.h` 被 git 忽略；正式上线前还需要补一个可提交的默认
manifest URL，并在正式构建中关闭 HTTP OTA。

---

## 尺寸与内存

固件约 **1.5 MiB**，OTA 槽 1.94 MiB（余量约 23%，约 449 KB）。大头是网络栈
（wpa_supplicant / TF-PSA-Crypto / lwIP），不是应用代码。

应用侧最大的单项是**字库**：正文 16px 约 185 KB，因为地名要覆盖省 ∪ 市 ∪ 区县
（1297 字）；每多一个 16px 字形约 **128 字节**（4bpp 16×16 点阵）。天气图标是
70 个 40x40 A4 掩码，共约 **55 KB**。

已做的三项体积取舍 —— **关掉 LVGL 默认主题**（它引用每个 widget 类，是 LVGL 链接后
达 ~260 KB 的主因）、**让 LVGL 走系统堆**（自带分配器静态占 64 KB DRAM）与
**`-Os` 编译**（比 `-Og` 省约 126 KB，断言仍开启）——各自的代价与实测数字记在
`sdkconfig.defaults` 的注释里。

---

## 界面设计要点

- **字体优先，位图只作补充**：文字走字库；天气图标用 QWeather 官方 SVG 转成的
  40x40 **A4 掩码 + recolor**（一套素材覆盖深色/浅色主题），再接「小字完整名」。
  确实需要位图时优先 A1/A4 + recolor，不存 RGB565 的两套主题副本。
- 太空人是 **1 位掩码（A1）+ recolor**：一套素材覆盖深色/浅色两个主题，比用 RGB565
  存两套省 16 倍。
- 天气图标来自 QWeather Icons v1.8.0（图标 CC BY 4.0），来源与改动见
  [`components/assets/icons/NOTICE.txt`](components/assets/icons/NOTICE.txt)。
- 界面上的字有**三个来源**，都必须进字库：源码里的字符串字面量（自动扫描）、
  **接口返回的数据**（风向/空气质量/天气现象，见 `components/assets/charset/runtime.txt`，
  手工维护）、地名（`city.txt`）。第二项最容易漏 —— 那些字永远不会出现在源码里。

排版规则（定宽必须实测、数字等宽、全角符号更宽等）见 CONVENTIONS §17。
