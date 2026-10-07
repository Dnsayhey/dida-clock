// 调度器竞争检测的方法学自证。
//
// 与 race_demo.cpp 同样的思路：**故意实现一个无封存的设计**，
// 用它反证"封存后并发注册全部被拒"这个检测手段确实有意义 ——
// 如果这个负对照在这套检验下也毫无问题，那说明检验本身是摆设。
//
// 负对照（UnsafeLiveRegisterScheduler）的形状：
//   * 内部 std::vector，Add 随时可以 emplace_back（无封存概念）；
//   * RunDue 与 Add 可能来自不同任务。
// 为了让演示不直接崩溃（便于观察），预先 reserve 了充足容量 ——
// 这样不会因重分配而野指针，但并发读写 vector 内部状态仍是真实的数据竞争，
// ThreadSanitizer 会报出来。
//
// 退出码 0 的条件是「负对照被检出问题」且「AppScheduler 干净」。

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "scheduler/app_scheduler.h"

namespace {

constexpr int kInitial = 8;
constexpr int kThreads = 4;
// AppScheduler 这一侧可以随便尝试（都会被拒绝）
constexpr int kAttempts = 20000;
// 负对照这一侧必须把尝试次数控制在预分配容量之内：
// 否则 vector 会重分配、进而真的野指针崩溃，演示就变成"比谁先崩"，
// 反而看不清竞争本身。留在容量内时不会崩，但并发读写 vector
// 内部状态仍是真实的数据竞争，ThreadSanitizer 会报出来。
constexpr int kUnsafeAttempts = 2000;
constexpr std::size_t kUnsafeReserve =
    kInitial + static_cast<std::size_t>(kThreads) * kUnsafeAttempts + 16;

std::atomic<uint64_t> g_now{0};

uint64_t NowMs() { return g_now.load(); }

struct Report {
  int accepted = 0;
  long iterations = 0;
};

// ---------------------------------------------------------------------------
// 负对照：无封存，Add 与 RunDue 可并发
// ---------------------------------------------------------------------------
class UnsafeLiveRegisterScheduler {
 public:
  struct Task {
    std::string name;
    uint32_t interval_ms;
    uint64_t last_run_ms;
  };

  UnsafeLiveRegisterScheduler() { tasks_.reserve(kUnsafeReserve); }

  // 没有封存概念：任何时候都能注册 —— 这正是要被 Seal 拒掉的行为
  void Add(const std::string& name, uint32_t interval_ms) {
    tasks_.push_back(Task{name, interval_ms, NowMs()});
  }

  void RunDue() {
    const uint64_t now = NowMs();
    for (auto& task : tasks_) {
      if (now - task.last_run_ms >= task.interval_ms) {
        task.last_run_ms = now;
      }
    }
    ++iterations_;
  }

  std::size_t task_count() const { return tasks_.size(); }

 private:
  std::vector<Task> tasks_;
  long iterations_ = 0;
};

// ---------------------------------------------------------------------------
// 封存版：封存后拒绝一切注册
// ---------------------------------------------------------------------------
Report RunUnsafe() {
  UnsafeLiveRegisterScheduler scheduler;
  for (int i = 0; i < kInitial; ++i) {
    scheduler.Add("init-" + std::to_string(i), 10);
  }

  std::atomic<bool> stop{false};
  std::atomic<int> accepted{0};
  std::atomic<long> iterations{0};

  std::vector<std::thread> registrars;
  for (int t = 0; t < kThreads; ++t) {
    registrars.emplace_back([&scheduler, &accepted, t]() {
      for (int i = 0; i < kUnsafeAttempts; ++i) {
        scheduler.Add("late-" + std::to_string(t) + "-" + std::to_string(i),
                      1000);
        accepted.fetch_add(1);  // 负对照照单全收
      }
    });
  }

  std::thread runner([&scheduler, &stop, &iterations]() {
    while (!stop.load()) {
      scheduler.RunDue();
      iterations.fetch_add(1);
    }
  });

  for (auto& r : registrars) {
    r.join();
  }
  stop.store(true);
  runner.join();

  return {accepted.load(), iterations.load()};
}

Report RunSafe() {
  scheduler::AppScheduler scheduler(NowMs);
  for (int i = 0; i < kInitial; ++i) {
    scheduler.Add("init-" + std::to_string(i), 10, []() {});
  }
  scheduler.Seal();

  std::atomic<bool> stop{false};
  std::atomic<int> accepted{0};
  std::atomic<long> iterations{0};

  std::vector<std::thread> registrars;
  for (int t = 0; t < kThreads; ++t) {
    registrars.emplace_back([&scheduler, &accepted, t]() {
      for (int i = 0; i < kAttempts; ++i) {
        if (scheduler.Add("late-" + std::to_string(t) + "-" + std::to_string(i),
                          1000, []() {})) {
          accepted.fetch_add(1);
        }
      }
    });
  }

  std::thread runner([&scheduler, &stop, &iterations]() {
    while (!stop.load()) {
      scheduler.RunDue();
      iterations.fetch_add(1);
    }
  });

  for (auto& r : registrars) {
    r.join();
  }
  stop.store(true);
  runner.join();

  return {accepted.load(), iterations.load()};
}

}  // namespace

int main() {
  std::printf("调度器竞争检测方法学自证\n");
  std::printf("============================================================\n");

  std::printf("\n[A] 运行期可注册的调度器（期望被检出并发问题）...\n");
  const Report unsafe = RunUnsafe();
  std::printf("    运行期注册被接受 %d 次，RunDue 迭代 %ld 次\n",
              unsafe.accepted, unsafe.iterations);

  std::printf("\n[B] scheduler::AppScheduler（封存后应全部拒绝）...\n");
  const Report safe = RunSafe();
  std::printf("    运行期注册被接受 %d 次，RunDue 迭代 %ld 次\n", safe.accepted,
              safe.iterations);

  std::printf(
      "\n------------------------------------------------------------\n");
  const bool old_design_is_exposed = unsafe.accepted > 0;
  const bool new_design_rejects_all = safe.accepted == 0;

  std::printf(
      "负对照被暴露（运行期注册确实会发生）: %s\n",
      old_design_is_exposed ? "\033[32m是\033[0m" : "\033[31m否\033[0m");
  std::printf(
      "封存版拒绝全部运行期注册            : %s\n",
      new_design_rejects_all ? "\033[32m是\033[0m" : "\033[31m否\033[0m");

  if (!old_design_is_exposed) {
    std::printf(
        "\n\033[31m警告：负对照的运行期注册未被触发，本次检验不具说服力。\033[0m\n");
  }
  if (!new_design_rejects_all) {
    std::printf(
        "\n\033[31m错误：封存版接受了运行期注册，封存机制失效。\033[0m\n");
  }

  return (old_design_is_exposed && new_design_rejects_all) ? 0 : 1;
}
