#pragma once

// 周期任务调度器。
//
// **并发约束：任务集合在启动期一次性注册完毕，之后不再变化。** 运行期注册会出现
// "一个任务 emplace_back（可能触发重分配）而另一个任务正在迭代同一个 vector"的
// 窗口 → 迭代器失效 → 崩溃。所以用构造消除并发，而不是加锁：
//   1. 注册只发生在启动期（单线程阶段）；
//   2. Seal() 之后任何 Add() 都被拒绝（并计数供调用方发现）；
//   3. 调度器只被**一个**任务拥有和迭代，不跨任务。
//
// 纯逻辑：不依赖 ESP-IDF 与平台时间（时钟由调用方注入），可在主机上单测，
// 包括用 ThreadSanitizer 检验"封存后并发注册"不再有害。

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace scheduler {

// 单调毫秒时钟。注入而非内置，既便于测试，也让本组件不绑死平台。
using MonotonicClock = std::function<uint64_t()>;

struct ScheduledTask {
  std::string name;
  uint32_t interval_ms = 0;
  uint64_t last_run_ms = 0;
  std::function<void()> callback;
};

class AppScheduler {
 public:
  // 不使用拷贝/移动：调度器应长期存活且地址稳定。
  AppScheduler(const AppScheduler&) = delete;
  AppScheduler& operator=(const AppScheduler&) = delete;

  explicit AppScheduler(MonotonicClock clock);

  // 注册一个周期任务。**只应在 Seal() 之前调用。**
  //
  // run_now=true 时立即执行一次，并以此作为首次计时起点
  // （否则第一次执行要等满一个完整周期）。
  //
  // 返回 false 表示注册被拒绝 —— 原因可能是：
  //   * 调度器已 Seal（运行期注册，正是要禁止的情形）
  //   * interval_ms == 0 或 callback 为空
  //   * name 重复
  // 调用方**必须检查返回值**，不要静默忽略。
  bool Add(const std::string& name, uint32_t interval_ms,
           std::function<void()> callback, bool run_now = false);

  // 封存：此后禁止注册。应在启动流程结束、进入调度循环前调用一次。
  // 可重复调用，无副作用。
  void Seal();

  bool Sealed() const { return sealed_.load(); }

  // 执行所有到期任务。**只应由拥有本调度器的那个任务调用。**
  // 内部一次性读取时钟，保证同一批任务的判定基准一致。
  void RunDue();

  // 诊断用
  //
  // 注意 task_count() 只在**未封存或已封存且无并发 Add 尝试**时可靠：
  // 封存后 tasks_ 不再变化，因此封存之后读取是安全的。
  std::size_t TaskCount() const { return tasks_.size(); }

  // 被拒绝的注册次数。启动后应检查它是否为 0。
  //
  // 为什么是原子：**拒绝路径本身就可能来自其他任务** —— 运行期误注册正是
  // 我们要捕获的情形，所以这个计数器必须能安全地被多任务并发递增。
  std::size_t RejectedRegistrationCount() const {
    return rejected_registrations_.load();
  }
  bool HasTask(const std::string& name) const;

  // 打印当前各任务的到期情况（由调用方决定输出方式，此处返回字符串）。
  std::string Describe() const;

 private:
  MonotonicClock clock_;
  std::vector<ScheduledTask> tasks_;  // 封存后不再变化
  std::atomic<bool> sealed_{false};
  std::atomic<std::size_t> rejected_registrations_{0};
};

}  // namespace scheduler
