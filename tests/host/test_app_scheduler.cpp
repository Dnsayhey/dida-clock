// AppScheduler 单测（主机运行）。
//
// 重点覆盖封存这条约束：
// 封存之后再有任何注册尝试，都必须是"被拒绝"而不是"破坏正在被迭代的容器"。

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "scheduler/app_scheduler.h"
#include "test_framework.h"

namespace {

// 可控的假时钟。用 atomic 以便并发测试中多线程安全读取。
struct FakeClock {
  std::atomic<uint64_t> now_ms{0};

  scheduler::MonotonicClock Get() {
    return [this]() { return now_ms.load(); };
  }
  void Advance(uint64_t delta) { now_ms.fetch_add(delta); }
};

}  // namespace

// ---------------- 注册期 ----------------

TEST_CASE(Scheduler_封存前可注册) {
  FakeClock clock;
  scheduler::AppScheduler scheduler(clock.Get());

  CHECK(scheduler.Add("a", 1000, []() {}));
  CHECK(scheduler.Add("b", 2000, []() {}));

  CHECK_EQ(scheduler.TaskCount(), 2u);
  CHECK_EQ(scheduler.RejectedRegistrationCount(), 0u);
  CHECK(!scheduler.Sealed());
  CHECK(scheduler.HasTask("a"));
  CHECK(!scheduler.HasTask("c"));
}

TEST_CASE(Scheduler_run_now立即执行一次) {
  FakeClock clock;
  scheduler::AppScheduler scheduler(clock.Get());

  int calls = 0;
  CHECK(scheduler.Add("a", 1000, [&calls]() { ++calls; }, /*run_now=*/true));
  CHECK_EQ(calls, 1);

  // 刚执行过，未到期前不应再执行
  scheduler.RunDue();
  CHECK_EQ(calls, 1);

  clock.Advance(1000);
  scheduler.RunDue();
  CHECK_EQ(calls, 2);
}

TEST_CASE(Scheduler_非法参数被拒绝) {
  FakeClock clock;
  scheduler::AppScheduler scheduler(clock.Get());

  CHECK(!scheduler.Add("zero-interval", 0, []() {}));
  CHECK(!scheduler.Add("null-callback", 1000, nullptr));
  CHECK(!scheduler.Add("", 1000, []() {}));
  CHECK_EQ(scheduler.TaskCount(), 0u);
  CHECK_EQ(scheduler.RejectedRegistrationCount(), 3u);
}

TEST_CASE(Scheduler_重名被拒绝) {
  FakeClock clock;
  scheduler::AppScheduler scheduler(clock.Get());

  CHECK(scheduler.Add("dup", 1000, []() {}));
  CHECK(!scheduler.Add("dup", 2000, []() {}));
  CHECK_EQ(scheduler.TaskCount(), 1u);
  CHECK_EQ(scheduler.RejectedRegistrationCount(), 1u);
}

// ---------------- 封存 ----------------

TEST_CASE(Scheduler_封存后拒绝注册) {
  FakeClock clock;
  scheduler::AppScheduler scheduler(clock.Get());

  CHECK(scheduler.Add("a", 1000, []() {}));
  scheduler.Seal();
  CHECK(scheduler.Sealed());

  // 运行期再注册：必须被拒绝，而不是去动正在被迭代的容器
  CHECK(!scheduler.Add("late", 1000, []() {}));
  CHECK_EQ(scheduler.TaskCount(), 1u);
  CHECK_EQ(scheduler.RejectedRegistrationCount(), 1u);
}

TEST_CASE(Scheduler_封存可重复调用) {
  FakeClock clock;
  scheduler::AppScheduler scheduler(clock.Get());

  scheduler.Seal();
  scheduler.Seal();
  CHECK(scheduler.Sealed());
  CHECK_EQ(scheduler.RejectedRegistrationCount(), 0u);
}

// ---------------- 到期判定 ----------------

TEST_CASE(Scheduler_未到期不执行) {
  FakeClock clock;
  scheduler::AppScheduler scheduler(clock.Get());

  int calls = 0;
  CHECK(scheduler.Add("a", 1000, [&calls]() { ++calls; }));

  clock.Advance(999);
  scheduler.RunDue();
  CHECK_EQ(calls, 0);

  clock.Advance(1);
  scheduler.RunDue();
  CHECK_EQ(calls, 1);
}

// 注意语义：**每次 RunDue 中每个任务至多执行一次**（见下一个用例）。
// 这是有意设计 —— 避免长时间停顿（例如配网阻塞几十秒）之后爆发式补跑。
TEST_CASE(Scheduler_多任务各自独立计时) {
  FakeClock clock;
  scheduler::AppScheduler scheduler(clock.Get());

  int fast = 0;
  int slow = 0;
  CHECK(scheduler.Add("fast", 100, [&fast]() { ++fast; }));
  CHECK(scheduler.Add("slow", 1000, [&slow]() { ++slow; }));

  clock.Advance(100);
  scheduler.RunDue();
  CHECK_EQ(fast, 1);
  CHECK_EQ(slow, 0);

  // 每轮只执行一次，因此连续三轮使 fast 增加到 4，而 slow 在第 3 轮首次到期
  clock.Advance(100);
  scheduler.RunDue();
  clock.Advance(100);
  scheduler.RunDue();
  CHECK_EQ(fast, 3);
  CHECK_EQ(slow, 0);

  // 推进到 1000ms：slow 到期
  clock.Advance(700);
  scheduler.RunDue();
  CHECK_EQ(fast, 4);
  CHECK_EQ(slow, 1);
}

TEST_CASE(Scheduler_一次RunDue内不会重复执行同一任务) {
  FakeClock clock;
  scheduler::AppScheduler scheduler(clock.Get());

  int calls = 0;
  CHECK(scheduler.Add("a", 10, [&calls]() { ++calls; }));

  // 时间跳过很多个周期，但一次 RunDue 只执行一次
  clock.Advance(1000);
  scheduler.RunDue();
  CHECK_EQ(calls, 1);
}

// 时钟回绕安全性：无符号相减，跨回绕点仍得到正确的间隔
TEST_CASE(Scheduler_时钟回绕安全) {
  FakeClock clock;
  clock.now_ms.store(UINT64_MAX - 500);
  scheduler::AppScheduler scheduler(clock.Get());

  int calls = 0;
  CHECK(scheduler.Add("a", 1000, [&calls]() { ++calls; }));

  clock.Advance(400);
  scheduler.RunDue();
  CHECK_EQ(calls, 0);

  clock.Advance(600);  // 越过 UINT64_MAX 回绕
  scheduler.RunDue();
  CHECK_EQ(calls, 1);
}

// ---------------- 并发回归测试 ----------------

// 封存之后，多个任务同时尝试注册，另一个任务在跑 RunDue。
// 修复后的实现必须：全部拒绝、容器不变、无数据竞争（TSan 应无警告）。
TEST_CASE(Scheduler_封存后并发注册全部被拒且不破坏容器) {
  FakeClock clock;
  scheduler::AppScheduler scheduler(clock.Get());

  // 启动期注册若干任务并封存
  constexpr int kInitialTasks = 8;
  std::atomic<int> executed{0};
  for (int i = 0; i < kInitialTasks; ++i) {
    CHECK(scheduler.Add("task-" + std::to_string(i), 10,
                        [&executed]() { executed.fetch_add(1); }));
  }
  scheduler.Seal();

  constexpr int kRegistrarThreads = 4;
  constexpr int kAttemptsEach = 5000;
  std::atomic<bool> stop{false};
  std::atomic<int> accepted{0};

  // 注册者：全部应当失败
  std::vector<std::thread> registrars;
  for (int t = 0; t < kRegistrarThreads; ++t) {
    registrars.emplace_back([&scheduler, &accepted, t]() {
      for (int i = 0; i < kAttemptsEach; ++i) {
        if (scheduler.Add("late-" + std::to_string(t) + "-" + std::to_string(i),
                          1000, []() {})) {
          accepted.fetch_add(1);
        }
      }
    });
  }

  // 迭代者：持续 RunDue，模拟 net 任务在遍历任务表
  std::thread runner([&scheduler, &stop]() {
    while (!stop.load()) {
      scheduler.RunDue();
    }
  });

  for (auto& r : registrars) {
    r.join();
  }
  stop.store(true);
  runner.join();

  // 核心断言：没有任何运行期注册被接受，任务数不变
  CHECK_EQ(accepted.load(), 0);
  CHECK_EQ(scheduler.TaskCount(), static_cast<std::size_t>(kInitialTasks));
  CHECK_EQ(scheduler.RejectedRegistrationCount(),
           static_cast<std::size_t>(kRegistrarThreads * kAttemptsEach));
}
