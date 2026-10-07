// 竞争检测方法学的自证程序。
//
// 目的：证明"全字段一致性"这个检测手段**真的能抓出数据竞争**，
// 而不是一个永远不会失败的摆设。
//
// 它同时跑两个实现：
//   A. UnsafeSharedStore —— 负对照：写入端逐个字段赋值，
//      读取端按值整体拷贝，全程**不加锁**。期望：被检出撕裂。
//   B. store::WeatherStore —— 本项目的实现。期望：零撕裂。
//
// 退出码 0 的条件是「A 被检出问题」且「B 完全干净」。
// 若 A 检不出问题，说明测试手段无效，本程序会失败。
//
// 配合 ThreadSanitizer 运行还能从内存层面确证 A 存在数据竞争、B 没有。

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "store/weather_store.h"

namespace {

constexpr int kWriterThreads = 4;
constexpr int kReaderThreads = 4;
constexpr int kIterations = 30000;

std::string MakeTag(int generation) {
  // 足够长以确保堆分配，让指针级撕裂成为可观测风险
  return "gen-" + std::to_string(generation) + std::string(96, 'x');
}

weather::WeatherNow MakeNow(const std::string& tag) {
  weather::WeatherNow now;
  now.temp = tag;
  now.feelsLike = tag;
  now.icon = tag;
  now.text = tag;
  now.windDir = tag;
  now.windScale = tag;
  now.humidity = tag;
  now.vis = tag;
  return now;
}

bool AllFieldsConsistent(const weather::WeatherNow& now) {
  return now.feelsLike == now.temp && now.icon == now.temp &&
         now.text == now.temp && now.windDir == now.temp &&
         now.windScale == now.temp && now.humidity == now.temp &&
         now.vis == now.temp;
}

// ---------------------------------------------------------------------------
// A. 负对照：无锁的读写
// ---------------------------------------------------------------------------
// 写入端逐个字段给 std::string 赋值，读取端按值拷贝整个结构体，
// 两边都没有锁 —— 这正是必须能被检出的写法。
class UnsafeSharedStore {
 public:
  // 模拟 parseWeatherNowResp 的逐字段赋值：中途存在部分更新的可见状态
  void WriteTag(const std::string& tag) {
    data_.now.temp = tag;
    data_.now.feelsLike = tag;
    data_.now.icon = tag;
    data_.now.text = tag;
    data_.now.windDir = tag;
    data_.now.windScale = tag;
    data_.now.humidity = tag;
    data_.now.vis = tag;
  }

  // 模拟 getWeatherNow()：按值返回，构造拷贝的过程中源仍在被改写
  weather::WeatherNow ReadNow() const { return data_.now; }

 private:
  store::WeatherSnapshot data_;  // 被两个线程无保护地共享
};

struct Result {
  long reads = 0;
  long torn = 0;
};

// A：无锁实现
Result RunUnsafe() {
  UnsafeSharedStore unsafe_store;
  unsafe_store.WriteTag(MakeTag(0));

  std::atomic<bool> stop{false};
  std::atomic<long> reads{0};
  std::atomic<long> torn{0};

  std::vector<std::thread> writers;
  for (int t = 0; t < kWriterThreads; ++t) {
    writers.emplace_back([&unsafe_store, &stop, t]() {
      for (int i = 0; i < kIterations && !stop.load(); ++i) {
        unsafe_store.WriteTag(MakeTag(t * 1000000 + i));
      }
    });
  }

  std::vector<std::thread> readers;
  for (int t = 0; t < kReaderThreads; ++t) {
    readers.emplace_back([&unsafe_store, &stop, &reads, &torn]() {
      while (!stop.load()) {
        const weather::WeatherNow now = unsafe_store.ReadNow();
        if (!AllFieldsConsistent(now)) {
          torn.fetch_add(1);
        }
        reads.fetch_add(1);
      }
    });
  }

  for (auto& w : writers) {
    w.join();
  }
  stop.store(true);
  for (auto& r : readers) {
    r.join();
  }
  return {reads.load(), torn.load()};
}

// B：本项目的加锁实现
Result RunSafe() {
  store::WeatherStore safe_store;
  safe_store.SetCurrentConditions(MakeNow(MakeTag(0)));

  std::atomic<bool> stop{false};
  std::atomic<long> reads{0};
  std::atomic<long> torn{0};

  std::vector<std::thread> writers;
  for (int t = 0; t < kWriterThreads; ++t) {
    writers.emplace_back([&safe_store, &stop, t]() {
      for (int i = 0; i < kIterations && !stop.load(); ++i) {
        safe_store.SetCurrentConditions(MakeNow(MakeTag(t * 1000000 + i)));
      }
    });
  }

  std::vector<std::thread> readers;
  for (int t = 0; t < kReaderThreads; ++t) {
    readers.emplace_back([&safe_store, &stop, &reads, &torn]() {
      while (!stop.load()) {
        const store::WeatherSnapshot snap = safe_store.Snapshot();
        if (!AllFieldsConsistent(snap.now)) {
          torn.fetch_add(1);
        }
        reads.fetch_add(1);
      }
    });
  }

  for (auto& w : writers) {
    w.join();
  }
  stop.store(true);
  for (auto& r : readers) {
    r.join();
  }
  return {reads.load(), torn.load()};
}

}  // namespace

int main() {
  std::printf("竞争检测方法学自证\n");
  std::printf("============================================================\n");

  std::printf("\n[A] 无保护的共享实现（期望被检出撕裂）...\n");
  const Result unsafe = RunUnsafe();
  std::printf("    读取 %ld 次，撕裂 %ld 次\n", unsafe.reads, unsafe.torn);

  std::printf("\n[B] store::WeatherStore（期望零撕裂）...\n");
  const Result safe = RunSafe();
  std::printf("    读取 %ld 次，撕裂 %ld 次\n", safe.reads, safe.torn);

  std::printf(
      "\n------------------------------------------------------------\n");
  const bool detection_works = unsafe.torn > 0;
  const bool fix_is_clean = safe.torn == 0;

  std::printf("检测手段有效（无锁实现被抓到）: %s\n",
              detection_works ? "\033[32m是\033[0m" : "\033[31m否\033[0m");
  std::printf("修复有效（加锁实现零撕裂）    : %s\n",
              fix_is_clean ? "\033[32m是\033[0m" : "\033[31m否\033[0m");

  if (!detection_works) {
    std::printf(
        "\n\033[31m警告：无锁实现未被检出，说明本测试手段不足以证明修复有效。\033[0m\n");
  }
  if (!fix_is_clean) {
    std::printf("\n\033[31m错误：加锁实现出现撕裂，修复无效。\033[0m\n");
  }

  return (detection_works && fix_is_clean) ? 0 : 1;
}
