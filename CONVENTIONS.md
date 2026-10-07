# dida-clock 代码规范与架构约定

这份文档是本项目约定的**单一出处**。它是从实际踩过的坑里长出来的，不是通用模板——
每条规则下面都说了"为什么"，因为不写原因，几个月后就会有人（包括我自己）把它当成无意义的限制绕过去。

**可执行性**：凡能用工具检查的规则，都做成了 `scripts/check_conventions.sh`，不要让约定停留在文字上。

```bash
./scripts/check_conventions.sh         # 检查
./scripts/check_conventions.sh --fix   # 检查并修正格式
```

---

## 1. 分层与依赖方向

### 1.1 分层定义

| 层 | 组件 | 性质 |
|---|---|---|
| **L0 纯领域逻辑** | `weather`（解析/URL）、`store`、`scheduler`、`storage/device_config`、`decompress/decompress_format`、`backlight/backlight_policy`、`portal/portal_page`、`portal/dns_message` | **无平台依赖，主机可测** |
| **L1 平台适配** | `net`、`storage/nvs_config_store`、`decompress/decompress_miniz`、`display`、`board`、`input`、`backlight/backlight_device`、`portal`（HTTP/DNS/AP 部分） | 封装 ESP-IDF 与硬件 |
| **L2 应用编排** | `pipeline`（HTTP → 解压 → 解析 → 入库）、`app`（UI 应用逻辑：状态机/命令/视图数据） | 组合 L0 + L1 |
| **L3 界面** | `pages`、`assets` | 只依赖 L2 提供的视图数据 |
| **L4 装配** | `main` | 唯一"知道所有东西"的地方 |

依赖**只能向下**。同层之间不得互相依赖。

**`pipeline` 与 `app` 的分界**：`pipeline` 是**数据流水线**（会做阻塞 I/O），
`app` 是**UI 应用逻辑**（纯决策与格式化）。这条分界让 `app` 不必依赖
`net` / `decompress` / `storage`，只剩 `store` + `weather` 两个依赖。

### 1.2 允许与禁止

```
pages     -> app, assets                     ✓
             net, storage, display, board    ✗ 禁止
app       -> store, weather,                 ✓
             input, backlight                ✓（下层服务；app 在其之上）
             net, storage, display, board    ✗ 禁止
pipeline  -> net, weather, store,
             decompress, storage             ✓
main      -> 任意                            ✓（装配层特权）
```

**`pages` 不得依赖 `net` / `storage` / `display` / `board`。**

理由：页面层一旦能直接读配置、发请求、碰硬件，"数据与渲染解耦"就名存实亡——
天气数据竞态正是从"UI 直接读共享数据"开始的。
页面要什么，就由 L2 提供一个**纯文本的视图数据结构**（如 `app::WeatherPageViewData`）。

**允许的例外**：页面可以经由 `store` 的公开头文件间接拿到**纯数据类型**（如 `weather::WeatherNow`）。
数据结构和**服务**不是一回事——禁的是依赖服务，不是依赖类型。

### 1.3 纯逻辑与平台代码：按**文件**分界

一个组件可以同时含纯文件和平台文件，但纯文件必须自证：

> **纯文件不得 `#include` 任何 `esp_*` 头。**

例如 `storage` 组件里，`device_config.cpp` 是纯的，`nvs_config_store.cpp` 不是；
`decompress` 里 `decompress_format.cpp` 是纯的，`decompress_miniz.cpp` 不是。

**"纯文件"的清单就是 `tests/host/run_tests.sh` 的 `SOURCES`** —— 单一事实来源，
`check_conventions.sh` 会校验这些文件确实没有引入 ESP-IDF 头。

**为什么这条最重要**：只有纯文件能在开发机上跑、能跑 ThreadSanitizer。
两个并发缺陷之所以能被**证明**修好了，靠的就是这份纯粹性。
一旦某个纯文件偷偷引入了 `esp_log.h`，它就永久失去了被验证的能力。

### 1.4 用 `PRIV_REQUIRES` 让构建系统强制分层

**这是本项目的一条硬性做法，不是建议。**

IDF 的 `REQUIRES` 是**传递可见**的：如果 `app` 用 `REQUIRES net`，那么依赖 `app` 的
`pages` 也能 `#include "net/..."` 并编译通过——分层就只是纸面约定。

**规则**：
- 公开头文件里真正需要的东西 → `REQUIRES`
- 只在 `.cpp` 里用的东西 → `PRIV_REQUIRES`

**已实测的效果**：`pipeline` 把 `weather`/`decompress`/`net`/`esp_timer` 放进
`PRIV_REQUIRES`，只把公开头文件真正需要的 `storage`/`store` 留在 `REQUIRES`。
于是在 `pages` 里 `#include "net/network_status.h"` 会直接**编译失败**：

```
fatal error: net/network_status.h: No such file or directory
```

（同样地，`app` 现在是 `REQUIRES store weather input backlight`、
无 `PRIV_REQUIRES` —— 它的公开头文件只用到这四个。）

分层从"靠自觉 + grep"变成了"**编译器不放过**"。新增组件时请照此办理。

### 1.5 依赖倒置：注入而非直连

纯逻辑需要外部能力时，**注入**，不要直连：

| 需要什么 | 做法 | 例子 |
|---|---|---|
| 时间 | 注入返回毫秒的函数 | `AppScheduler(MonotonicClock)` |
| 数据来源 | 注入回调 | `RealTimeWeatherPage(DataSource)` |
| 共享状态 | 只传快照 | `store::WeatherStore::Snapshot()` |

理由：`AppScheduler` 正因为时钟可注入，才能在主机上把"时钟回绕安全"这类边界测出来。

---

## 2. 命名（Google C++ 风格）

| 种类 | 规则 | 例 |
|---|---|---|
| 类型/类/结构体 | `PascalCase` | `WeatherStore`、`SyncState` |
| 函数/方法 | **一律 `PascalCase`，访问器不例外** | `Snapshot()`、`MarkReady()`、`Sealed()`、`TaskCount()` |
| 变量/参数/结构体字段 | `snake_case` | `location_id`、`timeout_ms` |
| 类成员变量 | `snake_case` + 尾随下划线 | `tasks_`、`root_`、`sealed_` |
| 常量 / `constexpr` | `k` + `PascalCase` | `kTag`、`kMaxReasonBytes` |
| 宏 | 全大写 + 下划线（尽量避免） | `QWEATHER_API_KEY` |
| 命名空间 | 全小写，**一个组件一个** | `store`、`scheduler`、`pages` |
| 文件名 | `snake_case` | `weather_store.cpp`、`theme_settings_page.h` |

**关于访问器**：Google 允许访问器写成变量样式（`sealed()`），但那会留下"哪些算访问器"的
判断题。本项目选择**所有函数一律 `PascalCase`**——规则没有灰色地带，才可能机械检查。

**踩过的坑**：不要用 `system` 作命名空间名——它与 C 库函数 `::system`（`<cstdlib>`）
冲突，报 `redefinition of 'system' as different kind of symbol`。

格式由 `.clang-format`（Google 基础）强制，见 §12。

---

## 3. 文件与目录布局

```
dida-clock/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── .clang-format                # Google 基础（见 §12 的两处必要改动）
├── CONVENTIONS.md               # 本文档
├── main/                        装配层
│   ├── app_main.cpp             app_main：只按顺序装配，不含逻辑
│   ├── runtime.{h,cpp}          跨任务状态 + InitNvs / UptimeMs / ApplyBacklight
│   ├── startup_task.{h,cpp}     StartupTask：RunStartupSequence() + RunSchedulerLoop()
│   ├── startup_flow.{h,cpp}     启门户 / 存配置 / 导航决策
│   └── ui_wiring.{h,cpp}        页面对象、注册、切页、视图数据、命令处理、按键轮询
├── components/<name>/
│   ├── CMakeLists.txt
│   ├── idf_component.yml        有托管依赖时才需要
│   ├── include/<name>/*.h       #include 路径与组件同名
│   └── *.cpp
├── scripts/check_conventions.sh
└── tests/host/                  主机测试（不需开发板）
```

**装配层也要拆。** 把启动顺序、资源生命周期、页面接线全塞进一个 `main.cpp`，
结果是**改任何一处都要读懂全部**，而且没法为其中任何一段单独写测试。
拆分原则是**按"谁能单独读懂"切**：装配顺序、跨任务状态、启动流程、UI 接线各成一体。
`app_main()` 里应当只剩"按顺序调用"，一眼能看出启动次序。

**头文件必须放在 `include/<组件名>/` 下**，且 `#include` 时带组件名前缀：

```cpp
#include "store/weather_store.h"   // ✓
#include "weather_store.h"          // ✗ 会与其它组件重名
```

理由：IDF 把所有组件的 include 目录平铺，不加前缀必然重名冲突。

---

## 4. 线程契约

**每个组件的头文件必须声明"哪个任务可以调用哪些方法"。** 这不是可选项。

本项目的任务模型：

| 任务 | 职责 | 禁止 |
|---|---|---|
| LVGL 任务（`esp_lvgl_port` 拥有） | 唯一的 LVGL 调用者：`lv_timer_handler`、页面创建、widget 更新 | **任何阻塞 I/O** |
| `StartupTask` | 全部阻塞 I/O：WiFi、NTP、HTTP、周期调度 | **任何 LVGL 调用** |

**最高优先级规则**：任何可能阻塞的调用只允许出现在 `StartupTask`。
LVGL 任务必须永远能在毫秒级回到 `lv_timer_handler()` —— 一次 HTTP 往返是
秒级的，放进去就是天气页卡死几秒。

头文件里的契约要写成这样（照抄 `WeatherStore` 的写法）：

```cpp
// ---------------- 写入侧（只应由启动任务调用）----------------
void SetCurrentConditions(const weather::WeatherNow& now);
// ---------------- 读取侧（UI 任务调用）----------------
WeatherSnapshot Snapshot() const;
```

跨任务同步改 UI 时**不要直接改 widget**，应通过消息投递让 UI 任务代做。
必须同步改时，用 `DisplayDevice::Lock()` / `Unlock()`。

---

## 5. 跨任务共享状态：只允许"快照"

**任何被多个任务访问的可变状态，必须通过锁保护的快照访问器暴露。**

```cpp
// 写侧：锁内整体替换
void Publish(const Status& s);
// 读侧：返回**拷贝**，锁外自由使用
Status Get() const;
```

**禁止**：
- 返回内部指针或引用 —— 那等于把内部状态的地址交出去，读写之间必然撕裂
- 暴露裸的共享 `std::string` / 容器
- 读侧在锁内做格式化或渲染（会造成"数据锁 → LVGL 锁"的嵌套顺序问题）

已按此实现的：`store::WeatherStore`、`net::NetworkStatus`、`net::WifiStation` 的访问器。

**为什么**：跨任务并发改写 `std::string` 的堆缓冲会导致 use-after-free。
实测无锁实现下 11,373,924 次读取中有 **9,497,668 次撕裂**，
ThreadSanitizer 报出 24 条竞争，摘要指向 `std::string::__set_long_size`。

---

## 6. 初始化分阶段与对象生命周期

### 6.1 三阶段：setup → seal → run

初始化期**单线程**、允许注册；进入循环前 `Seal()`；之后**禁止**任何注册。

```cpp
scheduler.Add("weather_now", kInterval, callback, /*run_now=*/true);
// ... 全部注册 ...
scheduler.Seal();          // 之后 Add() 一律被拒并计数
if (scheduler.RejectedRegistrationCount() != 0) { ESP_LOGE(...); }
while (true) { scheduler.RunDue(); vTaskDelay(...); }
```

**为什么**：运行期注册会同时来自 UI 任务与启动任务，
一边 `emplace_back` 一边迭代 → 迭代器失效 → 崩溃。
实测无锁对照接受 **8,000 次**运行期注册、TSan 报 4 条竞争，
摘要指向 `__vector_layout::__set_bound_using_pointer`。

**推论**：凡是"运行期可变集合 + 跨任务访问"，都要警惕。优先用构造消除并发，而不是加锁。

### 6.2 对象生命周期

> **`app_main` 返回后主任务栈会被释放。需要跨任务存活的对象不能是它的局部变量。**

必须放在文件作用域（或堆上）：

```cpp
// 文件作用域
store::WeatherStore g_weather_store;
display::DisplayDevice g_display;          // ← 不能是 app_main 的局部变量：
                                           //   app_main 返回后再 Lock() 就是 use-after-free
```

### 6.3 `app_main` 返回后由谁继续跑

`app_main` 返回 → 主任务被删除，但 LVGL 任务、`StartupTask`、`esp_timer` 继续运行。
不要依赖"`app_main` 还在"这个假设。

---

## 7. 错误处理

**IDF 默认禁用 C++ 异常，因此不要 `throw`。**

按边界分两种风格，不要混：

| 位置 | 风格 |
|---|---|
| ESP-IDF 调用边界 | `esp_err_t` + `ESP_ERROR_CHECK`（不可恢复）/ `ESP_RETURN_ON_ERROR`（可上报） |
| 纯逻辑 | 带**错误分级**的结果类型 |

纯逻辑的结果类型要能区分失败原因，不要只给一个 `bool`：

```cpp
enum class ParseError { kNone, kInvalidJson, kApiCodeNotOk, kMissingData };
struct ParseResult { bool ok; ParseError error; std::string message; std::string api_code; };
```

**为什么**：只区分"成功/失败"时页面只能显示"同步失败"，
无法判断是网络、HTTP、解压还是解析断的。

**同步状态要携带原因**：`SyncState.message` 会把 `network: 超时` / `decompress: ...` /
`api_code_not_ok: 204` 这类信息一路带到调用方，而不是只留一个"同步失败"。

---

## 8. 资源所有权

- **RAII**：`std::lock_guard` 管锁、`std::unique_ptr` 管所有权、
  `decompress` 里用 `std::unique_ptr<cJSON, CJsonDeleter>` 管 cJSON 节点
- **裸指针只表示"不拥有"**：`Page*`、`esp_lcd_panel_handle_t`
- **删除拷贝语义要有理由**：`WeatherStore` / `AppScheduler` 内含 `std::mutex`，
  本就不该被搬动，所以 `= delete` 拷贝与移动
- **NVS / LVGL 句柄**：每次操作自己开关（见 `NvsSession`），不要长期持有

---

## 9. 头文件纪律

- **include what you use**：不靠传递包含
- 公开头文件尽量只包含 `<string>` 这类标准库 + 本组件自己的类型
- 能用前向声明就别 include
- **公开头文件里出现什么，就决定了谁必须依赖你**——所以公开头越薄，分层越干净
  （§1.4 的 `PRIV_REQUIRES` 之所以可行，就是因为 `app` 的公开头只需要 `store`/`storage`）

---

## 10. 日志

- 每个文件一个 `constexpr char kTag[]`
- 等级：`ESP_LOGI` 正常流程 / `ESP_LOGW` 可恢复异常 / `ESP_LOGE` 需要干预
- **不要用日志代替状态上报**：需要给用户看的信息必须进 `WeatherStore` 的 `SyncState`，
  不能只打在串口
- 敏感信息（Wi-Fi 密码、API Key）**永不打印**

---

## 11. 注释

- **标识符用英文，注释用中文**
- **注释解释"为什么"，不解释"是什么"**。代码本身说明是什么
- 遇到反直觉的地方**必须**留注释。本项目里这类地方特别多，例如：
  - 和风天气**无条件**返回 gzip（不发 `Accept-Encoding` 也压缩）
  - 空气质量端点**没有** `code` 字段，不能统一校验业务码
  - LVGL 自带分配器会静态占用 64 KB DRAM
  - CLIB 与 BUILTIN 内存实现的取舍
- 文件头写清楚该文件的**并发约定**与**分层位置**
- **中文注释不会被格式化器重排**（见 §12）
- **不写历史叙事**。这是最容易被写出来、也最没价值的一类注释：

  ```cpp
  // ✗ 以前这里硬编码黑色，浅色主题下黑底黑字，所以改成下面这样
  // ✓ 底色取自当前主题：Create() 期间 OnThemeChanged() 尚未被调用
  ```

  代码描述的是**当前状态**，读者不需要知道它曾经错过。
  但**约束**必须留："必须这样，否则会那样" —— 它防止后人把 bug 改回去。

  **判据**：把"以前 / 原来 / 曾经 / 实机踩过"这些词删掉后，
  如果剩下的话仍然解释了**为什么不能换种写法**，那它是约束，该留；否则整条删掉。

  - **不写意义/使命叙事。**

    | ✗ | ✓ |
    |---|---|
    | "这是本项目存在的根本理由" | "一次 HTTP 往返是秒级的，放进渲染循环就是界面卡死几秒" |
    | "这正是本项目的核心目标" | （同上 —— 把后果写清楚就够了） |

    "我们很重视这件事"不提供任何约束。**读者要的是"必须这样，否则会那样"**，
    不是项目宣言；后果本身就说明了它有多重要。

    与上一条的关系：历史叙事讲"以前错过什么"，意义叙事讲"我们多么在意" ——
    两者都是**绕开技术内容去谈态度**。


---

## 12. 格式（`.clang-format`）

基础是 Google 风格，但有**两处必须保留的改动**——否则会损坏中文内容：

| 选项 | 值 | 为什么必须这样 |
|---|---|---|
| `ReflowComments` | `false` | 默认值 `Always` 会重排中文注释，甚至**把相邻两行合并** |
| `BreakStringLiterals` | `false` | 默认值 `true` 会**把中文界面文案切成多个字面量拼接** |

`clang-format` 按**字节**计算列宽，而中文在 UTF-8 里占 3 字节，所以它对 CJK 的处理天生不对。
关掉这两项后，中文注释与字符串保持原样，代码仍然正常折行。

**其它关键项**：`ColumnLimit: 80`、`IndentWidth: 2`、`PointerAlignment: Left`、
`AllowShortFunctionsOnASingleLine: All`。

生成的字体数据（`components/assets/font/*`）不参与格式化，见 `.clang-format-ignore`。

---

## 13. 禁用清单

| 禁用 | 替代 |
|---|---|
| C++ 异常（`throw`） | 返回结果类型 / `esp_err_t` |
| LVGL 任务里的阻塞调用 | 移到 `StartupTask` |
| 返回内部指针的访问器 | 返回快照拷贝 |
| 运行期任务注册 | 启动期注册 + `Seal()` |
| COM 之外的 `#include "esp_*"` 出现在纯文件里 | 注入平台能力 |

---

## 14. 测试约定

### 14.1 能在主机上测的，必须能在主机上测

`tests/host/` 不需要开发板、不需要 IDF 环境：

```bash
cd tests/host && ./run_tests.sh --tsan
```

### 14.2 并发必须用 ThreadSanitizer，且要**证明测试能失败**

只写一个"通过"的并发测试没有意义——它可能从来没触发过竞争。
本项目的方法是**同时实现一个故意写错的无锁对照**，要求：

- 无锁对照**必须被检出问题**（否则说明检测手段无效，程序自己失败）
- 新实现**必须零竞争**

已有两处：`race_demo.cpp`、`scheduler_race_demo.cpp`。

**同一条道理适用于所有自动检查。** 每加一项检查，都要**故意造一个反例验证它会
失败** —— 不会失败的检查等于没有，而且**比没有更糟：它让人以为受保护了**。

### 14.2.1 不是所有规则都该做成常驻检查

这条规则曾经被误用：为"注释里不许出现旧项目的名字"专门写了一项脚本检查，
后来**整个删掉了**。因为它搞错了类别：

| | 常驻检查 | 一次性清理 |
|---|---|---|
| 例子 | 格式、命名、分层、字库覆盖 | "把旧项目的痕迹清干净" |
| 特征 | **每次改动都要重新成立** | 做完就不再发生 |
| 手段 | 脚本 / 编译器 | 一次性的全量排查（人或子代理） |

那种"不许出现某个名字"的规则属于后者。项目已经从那个版本脱离，**新人不会去对比
一个他没见过的代码库**；而为了让脚本能"守住"这条，它反而必须**一直保留那些名字**
（白名单、反例、说明），成了旧项目在仓库里的永久纪念碑 —— 为了防住字符串，
把字符串供起来了。

**判据**：这条规则会不会在**未来每一次**改动中都需要重新验证？
会 → 做成检查；不会 → 一次性清理掉，**把原则写进规范就够了**。

### 14.2.2 验证要看**退出码**，不能只看输出片段

一次真实的教训：脚本打印了「用例: 221 个，失败: 0 个」，于是被记为"通过"；
而它的退出码一直是 **1** —— 后面还有一步编译失败，被 `grep '用例:'` 过滤掉了。

**"输出了我想看的那一行"不等于"这一步通过了"。** 判断任何检查/测试/构建的结果，
一律看**退出码**（`$?`、`echo $?`、`set -e`），输出只用来定位失败原因。

这条同样适用于本章 §14.2 的自证程序：它们必须**真的运行**过；
`race_demo` 与 `scheduler_race_demo` 一旦编译失败，`run_tests.sh` 会以非 0 退出 ——
而"221 用例全过"照样会打印出来。

### 14.3 用真实数据做回归

和风天气的样本放在 `tests/host/fixtures/`，是**真实抓取**的响应，不是手写。
手写样本容易"刚好符合实现假设"——真实数据已经打穿过两个 bug
（空气质量无 `code` 字段、pm2.5 名字带空格）。

### 14.4 有状态组件的测试构造

组件删除了拷贝/移动语义时（含 `std::mutex` 的类型），测试**就地构造 + 用接受引用的函数填充**：

```cpp
WeatherStore store;          // ✓
FillReadyStore(store);       //   填充函数接受引用

WeatherStore store = MakeReadyStore();   // ✗ 需要拷贝/移动
```

### 14.5 明确标注"哪些没被验证"

**实机验证与主机验证是两回事。** 主机测试全绿不代表设备能跑。
凡只有构建级保证的部分，必须在文档里显式写明，避免把"构建通过"误当成"能用"。

---

## 15. 决策记录

凡是非显然的选择，都要记录**决策 + 理由 + 实测数据**。
至少以下几条必须留痕，因为它们是**反直觉**的：

| 决策 | 关键理由 |
|---|---|
| 用 `esp_lvgl_port` 2.9.0 而非新版 `esp_lvgl_adapter` | 后者强制引入 FreeType 等 3 个公有依赖 |
| 用 `espressif/cjson` | **IDF 6.0 移除了内置 `json` 组件** |
| 用 ESP32-C3 **ROM miniz** 解压 | **IDF 没有内置 zlib**；ROM 只导出 `tinfl_*`，gzip 头须自剥 |
| 自定义 gzip 头解析 | `tinfl` 不支持 gzip 容器 |
| 关闭 LVGL 默认主题 | 它会引用每个 widget 类，拉入全部 widget 代码（省 91.6 KB） |
| LVGL 用 CLIB 内存实现 | 自带分配器静态占 64 KB DRAM |
| 双 OTA 槽各 1.75 MB | 4 MB flash 下不让 `factory` 分区参与 |
| 数据流水线独立成 `pipeline` 组件 | `namespace sync` 与 POSIX `sync()` 冲突（同 `system` → `scheduler`） |
| 界面字体用 LVGL 内置 Montserrat 16 | 它没有本项目需要的汉字；且英文比中文宽，20 号下长文案会超屏宽 |
| 所有 API 请求带 `lang=zh` | 界面是中文；不带这个参数会拿到英文，本地还得再翻译一层 |
| `swap_bytes=true` **且** `BGR=0` | RGB565 的 5-6-5 字段不按字节对齐，字节序与 BGR 位必须配对；四种组合只有这一种颜色正确 |
| WiFi 驱动初始化放在装配层 | STA 与强制门户是两条互斥路径，只有前者会调 `WifiStation::Begin()` |
| 表单体只读一次再解析 | `httpd_req_recv()` 会消费请求体，逐字段读取只有第一个有值 |

新增决策请追加到 §15 决策记录（本表），**不要只写在提交信息里**。

| 日期列的四个栏位 | 拆成日期 / 星期 / 天气 / 温度四列，而不是拼成一行文本 —— 比例字体下不同日期的宽度差 13px，拼一行时星期会左右飘 |
| 超长文本用滚动而非截断 | `LV_LABEL_LONG_MODE_SCROLL_CIRCULAR` 只在放不下时才滚，所以常见情况静止、极少数才动，信息不丢 |
| 界面配色分两套色表 | 鲜艳色在黑底上对比度 9.1、白底上只有 2.3；同一色调必须为浅色主题准备暗版 |
| 天气用「大字分类 + 小字完整名」 | 大字给辨识度（9 类），小字保留官方完整 text —— 雨量（小雨/中到大雨/暴雨）与雾霾的区别都不丢 |
| 天气分类取自 `icon` 而非 `text` | `icon` 是数字代码，与 `lang` 无关；用 `text` 做映射会在改语言时静默失效 |
| 数据刷新与动画各用一个定时器 | 两者周期差 30 倍，合并会拖慢按键采样（见 §18） |
| 地名优先显示 `name`（区县）而非 `adm2`（地级市） | 标签槽位只有 48px = 3 个汉字，adm2 有 **22.4%** 的取值超宽（"乌鲁木齐市""乌兰察布市"…）会被省略号截断，name 只有 **2.4%**。代价是字集从 438 扩到 1297 字，**实测固件 +100,288 B**（边际 128.2 B/字形）；省与市一并保留，避免 lookup 失败回退显示手输地名时缺字 |

---

## 16. 交付前检查清单

```bash
./scripts/check_conventions.sh         # 约定（分层/命名/格式/配色/字库覆盖）
python3 scripts/measure_text.py --all  # 固定宽度槽位是否都放得下
cd tests/host && ./run_tests.sh --tsan # 主机测试 + TSan
source ~/esp/idf61.sh && idf.py build  # 固件构建
```

**改动版面或配色时**，先用 HTML 原型画出效果再动代码 —— 配色与间距在文字里
判断不了，必须先看到图。流程见 [docs/界面原型渲染流程.md](docs/界面原型渲染流程.md)：

```bash
./scripts/render_mock.sh /tmp/mock/x.html   # 真·2x 渲染
# 然后用 read_image 把 PNG 读出来看 —— "渲染了"不等于"看过了"
```

原型只回答"**好不好看**"；"**放不放得下**"要另用 `measure_text.py --all` 复核，
因为浏览器的字体与设备字库不同，字符串宽度算不准。

**改动界面文案时**必须先重新生成字库，否则缺字在屏幕上就是空白：

```bash
python3 scripts/gen_fonts.py           # 会扫描源码 + 城市字集 + 运行期词汇
```

三项全绿才算完成。若改动涉及分区表，还需 `idf.py flash -a` 全量烧录。

**但"三项全绿"不等于"能跑"。** 本项目实机暴露过的缺陷里，**绝大多数都通过了构建与测试**——
因为它们是"**代码写对了，但那条路径从未被执行过**"：

| 从未执行的路径 | 为什么一直没被发现 |
|---|---|
| 配网（WiFi 未配置时才走） | 板子上一直存着 WiFi 配置，这条路径根本没跑过 |
| 浅色主题下的页面创建 | 一直跑在深色主题上 |
| gzip 解压 | 只有真实响应才会触发 |
| HTTPS 证书校验 | 主机测试里从未真连过 |

**所以第四项是：实机跑一遍受影响的路径。** 判断"哪些路径没被跑过"只需问一句：

> 这段代码在什么条件下才执行？那个条件在当前设备上**成立吗**？

不成立就必须**主动构造那个条件** —— 例如 `idf.py erase-flash` 清掉 NVS 去触发配网流程。

---

## 17. 界面布局与字库

### 17.1 固定宽度的槽位必须**实测**定宽，不能估算

凡是给 `lv_obj_set_width()` 设了确定宽度、又往里放变长文本的地方，宽度必须用
`scripts/measure_text.py` 算过：

```bash
python3 scripts/measure_text.py dida_cn_16 "08-08" "中到大雪" "20/30℃"
python3 scripts/measure_text.py --all      # 核对所有登记在册的槽位
```

**不能按字形的 `adv_w` 简单累加。** LVGL 的实际算法还做两件事：

1. **字距调整（kerning）参与**：`kv = (kvalue * kern_scale) >> 4`
2. **逐字形各自四舍五入到整像素**：`adv_w = (gdsc->adv_w + kv + 8) >> 4`

第 2 条尤其容易漏 —— 不是最后统一取整，5 个字形的误差能累积 2~5px。

**定宽还必须按最坏样本，不能按当天的值。** 日期列踩过两次：
`"1"` 只有 6px 而 `"0"/"8"/"9"` 有 10~11px，所以 `"10-10"` 是 40px 而
`"10-09"` 是 44px、`"08-08"` 是 48px。按前者定 42px 槽，当天就换行了。

宽度不够时**优先滚动而不是截断**：`LV_LABEL_LONG_MODE_SCROLL_CIRCULAR`
只在文字放不下时才滚，放得下就是静态的 —— 于是常见情况（短文本）完全静止，
只有极少数超长文本才动起来，信息不丢、画面也不吵。

### 17.2 字库要同时覆盖「源码文案」与「运行期数据词汇」

字库是**编译期固定**的，而界面上的字有两个来源：

| 来源 | 怎么保证覆盖 |
|---|---|
| 源码里的字符串字面量 | `scripts/gen_fonts.py` 自动扫描（跳过注释与 URL 里的 `//`） |
| **接口返回的数据** | `components/assets/charset/runtime.txt`，**必须手工维护** |
| 地名 | `components/assets/charset/city.txt`（官方城市列表的**省 ∪ 市 ∪ 区县**合并去重，1297 字） |

第二项是最容易漏的：风向「东风」、空气质量「轻度污染」、天气现象
「雷阵雨伴有冰雹」**永远不会出现在源码里**，只扫源码的字库一定缺这些字。
实机上漏过「风」，整条信息显示成「东 4级」。

`scripts/check_fonts.py` 会在约定检查里守住"源码用字 ⊆ 字库"。
检查范围是**所有非 ASCII 字符**（不限汉字）—— 实机漏过一个 `▸`(U+25B8)，
屏幕上就是方块。

改动文案后必须重新生成：`python3 scripts/gen_fonts.py`。

### 17.3 文字层次靠「颜色」而不是「亮度差」

`TextRole` 三个角色的分工要按**信息地位**判，不是按"看起来像不像标题"：

| 角色 | 用于 | 判断标准 |
|---|---|---|
| `kText` | 标题、**设置项的标签/字段名**、正文 | **不读它就无法理解这一屏** |
| `kDim` | 操作提示、状态细节、次要读数 | 不读它也不影响理解 |
| `kDanger` | 危险动作 | 唯一的警示 |

**设置行的标签属于 `kText`，不属于 `kDim`。** 把"标签"一律当成
caption 套 `kDim`，层次就会倒过来：解释性的标签（对比度 5.9）比它解释的
值（强调色 9.1）还暗，标题更是 21.0 —— 屏幕上最弱的一环恰好是用户必须先读的
那一环。实机反馈"深色下标签看不清"。

正确做法是**让值靠颜色跳出，而不是让标签暗下去**：值用 `accent`，标签用
`kText`，两者都清楚，层次由颜色承担。

对照：`kDim` 的恰当用法是底部那三行「单击切换亮度」—— 它们是辅助，
暗下去是对的。

### 17.4 "固定位置 + 浮动宽度"必然出问题

只要一个元素的坐标是**写死的常量**，而它旁边元素的宽度**随内容变化**，
两者迟早会在某个取值上撞车。这不是"调一下就好"，而是坐标系不匹配。

实时天气页踩过：秒的 x 写死 194，而时分是 `HH:MM`，宽度随数字浮动
（`1` 26px、`0`/`4` 47px）——

```
11:11  时间 120px  间距 62px   ← 太空
17:35  时间 162px  间距 20px
18:00  时间 181px  间距  1px   ← 贴住
04:44  时间 204px  间距-22px   ← 压在一起
```

实测全部 1440 个时刻里，**34% 的间距为负**，而且集中在 00~09 点
（每小时 36~48 分钟）。

**修法是消除浮动，不是调常量。** 对数字时钟就是**等宽数字**
（tabular figures）：把 0~9 的 `adv_w` 统一成最宽的那个，宽度就恒定了。
`gen_fonts.py` 的 `make_tabular()` 做这件事。

**注意：只统一 `adv_w` 不够。** LVGL 实际算的是 `adv_w + kv`，而数字之间
有非零字距（这个字库有 33 对，最大 -53/16 px）。必须**同时关掉 kern**，
否则等宽会被字距重新破坏。而且三张字距表要**整段删掉**，不能只把
`.kern_dsc` 指成 NULL —— 表会变成 "defined but not used"，在
`-Werror=unused-const-variable` 下直接编译失败。

**同类检查方法**：凡是页面上出现"固定 x 的标签 + 变长文本"，就用
`measure_text.py` 把所有实际取值遍历一遍，看间距的极值。

### 17.5 全角符号比数字宽得多

`℃` 的 `adv_w` 是 16px，而数字只有 6~11px；`%`、`：`、`（）` 同理。
按"跟一个数字差不多宽"给槽位，就会差那么几像素而换行 —— 表现为
"第一行的 ℃ 掉到下面了"，且只在特定数值下出现，极难复现。

**布局里凡涉及全角符号的槽位，宽度按 `measure_text.py` 的实测值留余量。**

---

## 18. 定时器

### 18.1 周期差数量级的任务不可共用一个定时器

一个 `lv_timer` 的回调里若同时做两件事，两件事就会被迫用**同一个周期**，
而周期只能取更快的那个。代价不是"多做了一点功"，而是**廉价但高频的那件事把
采样类任务饿死**：把「重建整页数据」（1s 一次）与「太空人换帧」（33ms）合在
一个 33ms 定时器里，就是每秒重建 30 次页面数据 —— 快照拷贝 + 字符串拼接 +
重设全部 label。LVGL 任务因此长期繁忙，**10ms 的按键轮询被反复推迟，
100~200ms 的短按整个被漏采**，表现为"双击偶尔变成单击"。

判断方法：**这个回调里有没有两件代价差一个数量级以上的事？** 有就拆开。

反过来，`OnLeave()` 里必须把**每一个**定时器都停掉 —— 隐藏页面继续以
30fps 换图是纯粹的浪费。

### 18.2 对时序敏感的输入采样，要保证回调真的跑得动

按键轮询依赖 10ms 定时器准时执行。**任何让 LVGL 任务长时间繁忙的东西
（高频定时器、重绘、页面切换）都会直接损伤按键采样**，而且损伤是
概率性的、只在特定操作节奏下出现。

排查这类问题不要靠猜：判别状态机只给出"分类结果"（单击/双击/长按），而
"是判错了还是没采到"这个问题只有**原始事件的时间戳**能回答。

---

## 附录 A：每条规则背后的坑

下面每一条都对应一次真实的失败。

| 规则 | 对应的真实失败 |
|---|---|
| §5 跨任务只允许快照 | 共享 `std::string` 的竞态：实测 84% 的读取会撕裂 |
| §6.1 启动期注册 + Seal | `vector` 边界竞争：无锁对照接受 8,000 次运行期注册 |
| §6.2 对象不能是 `app_main` 局部 | `DisplayDevice` 曾是局部变量，`app_main` 返回后 `Lock()` 即 use-after-free |
| §7 错误要分级 | 只回"同步失败"时无法判断断在哪一环 |
| §12 关掉 `ReflowComments` | clang-format 把中文注释合并、把界面文案切成多段字面量 |
| §2 不用 `system` 作命名空间 | 与 C 库 `::system` 冲突，编译失败 |
| §1.4 用 `PRIV_REQUIRES` | `pages` 能直接 include `net/`，分层形同虚设 |
| §14.2 证明测试能失败 | 只看输出片段会得出相反的结论（报告被 `tail` 截断，"0 警告"是假的） |
| §14.3 用真实数据 | 空气质量端点没有 `code` 字段，手写样本永远发现不了 |
| §9 公开头越薄越好 | `app` 的公开头只需要 `store`/`storage`，正因如此才能把 `net` 设为私有 |
| §2 命名空间避开 C 库符号（第二次） | `namespace sync` 与 POSIX `sync()` 冲突；组件只得改名 `pipeline` |
| §11 不写历史叙事 | 一轮 review 清掉 12 处"以前是 X，现在改成 Y"；读者要的是约束，不是病历 |
| 平台初始化放在装配层 | `esp_wifi_init()` 只在 `WifiStation::Begin()` 里调，而配网模式下 `Begin()` 从不执行 —— **强制门户完全起不来** |
| §6 初始化分阶段 | 扫描写在 `esp_wifi_start()` 之前，必然 `ESP_ERR_WIFI_NOT_STARTED`，WiFi 列表永远为空 |
| 第三方 API 的一次性资源 | `httpd_req_recv()` 会消费请求体；逐字段各读一次 → 只有第一个有值，**门户永远存不了配置** |
| 颜色 API 的输入格式 | `lv_color_hex()` 收 24 位 RGB888；传 16 位 RGB565 值会把橙色渲染成亮绿 |
| 颜色链路的两个开关必须配对 | RGB565 的 5-6-5 字段不按字节对齐，`swap_bytes` 与 `BGR` 位错配会渲染出"红/蓝/绿"这种无法从单一开关推出的现象 |
| 界面换语言要连带换字库 | 界面语言与字库字符集必须同步：中文界面配英文字库 → 缺字显示成方块；且同一字号下英文比中文宽得多，长文案会超屏宽 |
| §17.1 定宽必须用实测算 | 按字形 `adv_w` 累加算 "10-10" 是 39.4px 给了 42px 槽，而当天显示的 "10-09" 是 44px（`"1"` 只有 6px，`"0"/"8"/"9"` 有 10~11px）—— **拿错了样本**，日期列换行 |
| §17.1 漏掉 kerning 与逐字形取整 | LVGL 是每个字形各自 `(adv_w + kv + 8) >> 4`，不是最后统一取整；5 个字形的误差能累积 2~5px |
| §17.3 文字层次靠颜色不靠亮度 | 设置页把「标签」当 caption 用了 `kDim`(5.9)，比它解释的「值」(`accent` 9.1) 还暗，标题 21.0 —— 屏幕最弱的一环恰是必须先读的那一环，实机反馈深色下看不清 |
| §17.4 固定位置 + 浮动宽度 | 秒的 x 写死 194，而时分宽度随数字浮动 120~204px → 1440 个时刻里 34% 时间压到秒上（最严重 -22px），最宽的又空出 62px。只统一 adv_w 不够，还得关掉 kern（LVGL 算的是 adv_w + kv），且字距表要整段删否则 -Werror 报 unused |
| §17.5 全角符号比数字宽得多 | `℃` 是 16px 而数字只有 6~11px；温度槽按"跟一个数字差不多"给 56px，`16/26℃`(56.5px) 恰好溢出 0.5px 换行 —— 而且只在特定数值下出现 |
| §17.2 字库要覆盖运行期词汇 | 字集只扫源码字符串 + 城市名，漏了接口返回的风向「东风」—— 屏幕上显示成「东 4级」 |
| §17.2 缺字检查要不限汉字 | 启动页用 `▸`(U+25B8) 标当前进度，而检查只查汉字，屏幕上是个方块 |
| §17.2 生成与校验的判定必须一致 | `gen_fonts.py` 用 `is_cjk` 而 `check_fonts.py` 用 `needs_glyph`，两边不一致 → 生成的字库永远比校验认为需要的少几个全角标点 |
| **§18 周期差数量级的定时器不可合并** | 把「1s 重建整页数据」与「33ms 换帧」合成一个 33ms 定时器 → 每秒重建 30 次页面数据 → LVGL 任务长期繁忙 → **10ms 按键轮询被推迟，短按漏采**，表现为"双击偶尔变成单击" |
| §18.2 时序问题要看原始事件 | 状态机只输出分类结果，"判错了"与"根本没采到"从结果上看不出区别 |
