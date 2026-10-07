# 主机单测

这些测试针对的是**纯逻辑**部分（JSON 解析、URL 编码、`WeatherStore` 并发语义），
**不需要开发板，也不需要 ESP-IDF 环境** —— 直接用宿主编译器编译运行。

## 运行

```bash
./run_tests.sh          # 普通构建 + 运行
./run_tests.sh --tsan   # 额外做 ThreadSanitizer 校验（会慢一些）
```

cJSON 源码复用 IDF 组件管理器下载到 `managed_components/espressif__cjson/` 的那一份，
保证主机测试与固件使用的是同一个 JSON 实现。首次运行前需先在项目根执行一次
`idf.py build` 以拉取托管组件。

## 文件

| 文件 | 内容 |
|---|---|
| `test_framework.h` | 极简断言/运行框架，零第三方依赖 |
| `fixture_loader.h` | 读取 `fixtures/` 下的样本 |
| `weather_fixtures.h` | **手工构造**的边界样本（不完整 JSON、缺字段、超/不足 7 天等） |
| `fixtures/*.json` | **真实抓取**的 API 响应（明文） |
| `fixtures/*.json.gz` | 同一批响应的**真实 gzip 字节**（未解压的原始响应体） |
| `test_weather_api.cpp` | URL 编码与 4 个端点 URL 构造 |
| `test_weather_parser.cpp` | 解析逻辑与**错误分级**（手工样本，覆盖边界） |
| `test_weather_parser_real.cpp` | **真实响应**解析回归 |
| `test_weather_store.cpp` | `WeatherStore` 基本行为 + 并发不变式压力测试 |
| `test_app_scheduler.cpp` | `AppScheduler` 行为 + **封存后并发注册回归测试** |
| `test_decompress.cpp` | 压缩格式识别、gzip 头解析、真实响应的偏移验证 |
| `race_demo.cpp` | 方法学自证（WeatherStore） |
| `scheduler_race_demo.cpp` | 方法学自证（AppScheduler） |
| `test_main.cpp` | 主套件入口 |

## 为什么用真实响应

手写样本容易"刚好符合实现假设"。真实数据会暴露想当然的地方 —— 实际已暴露两个：

1. **空气质量端点没有 `code` 字段**。一刀切要求 `code=="200"` 会把全部合法响应判为失败。
2. **pm2.5 的名字实际是 `"PM 2.5"`（带空格）**，且 `primaryPollutant` 会是 `null`。

## 解压怎么测的

设备侧用 ESP32-C3 **ROM miniz** 的 `tinfl_*`，主机上不可用。因此拆成两层：

- `decompress_format.cpp`（**纯逻辑**：格式识别 + gzip 头解析）参与主机测试；
- `decompress_miniz.cpp`（调用 ROM）只在固件构建中编译。

主机侧用**系统 zlib** 作为参考实现，对每个真实 `*.json.gz` 算出 deflate 偏移后做裸 inflate，
结果必须与同名明文 fixture **逐字节相等**。这样就把"头部解析对不对"与"ROM miniz 好不好用"
解耦开，前者能被确定性验证。

ROM miniz 是否真的链接得上，由固件侧的启动自检（`main/selftest.cpp`）覆盖：
它内嵌一个 313 字节的真实 gzip 响应，开机解压并校验内容片段。

## 并发测试怎么做的

`WeatherStore` 的并发正确性用**不变式检测**验证：

- 写入端把快照的**全部字段**设为同一个"代号"；
- 读取端断言拿到的快照里全部字段互相一致；
- 若锁失效导致撕裂读，各字段会来自不同代号，断言立即失败。

代号刻意做成长字符串（绕过 `std::string` 的 SSO），使"指针级撕裂"成为**可观测的真实风险**，
而不是理论上存在。

`race_demo.cpp` 是这套方法学的**自证程序**：它同时跑两个实现 ——

- **A**：`UnsafeSharedStore`，故意错的那种写法（写入端逐字段赋值、读取端按值整体拷贝，
  全程不加锁）；
- **B**：`store::WeatherStore`，本项目的实现。

只有当「A 被检出撕裂」且「B 零撕裂」时才返回 0。**若 A 检不出问题，说明测试手段无效，
程序会主动失败** —— 避免出现"测试永远通过但从没验证过任何东西"的情况。

## 实测结果

| 实现 | 不变式撕裂 | TSan 警告 |
|---|---|---|
| 无锁（A：`UnsafeSharedStore`） | 9,497,668 / 11,373,924 读取 | **24** |
| `store::WeatherStore` | **0** / 785,559 读取 | **0** |

TSan 报告摘要指向 `std::__1::basic_string::__set_long_size` 与
`std::__1::char_traits<char>::assign` —— 与"跨任务并发改写 `std::string`
的堆缓冲"这一诊断吻合。

`Scheduler_封存后并发注册全部被拒且不破坏容器` 用同样的思路反证：
4 个线程各尝试 5,000 次运行期注册，同时另一个线程持续 `RunDue()`。
修复后的实现全部拒绝且容器不变，TSan 零警告。

`scheduler_race_demo.cpp` 是它的反证：一个**允许运行期注册**的调度器版本，
允许运行期 `push_back`。实测它**接受 8,000 次**运行期注册，
且 TSan 报出 4 条竞争，摘要指向
`std::__1::__vector_layout::__set_bound_using_pointer` / `__end_ptr()`
—— 正是"一边 `emplace_back` 一边迭代"导致的边界竞争。

> 一个实现细节：把注册限制在预分配容量内，是为了**避免演示变成比谁先崩** ——
> 否则 `vector` 重分配会直接踩坏内存（SIGABRT），而不是稳定地暴露竞争。
