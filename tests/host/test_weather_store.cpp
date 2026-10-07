// WeatherStore 单测（主机运行），含并发不变式压力测试。
//
// 并发测试的核心思路：写入端把快照的**全部字段**设为同一个"代号"，
// 读取端断言拿到的快照里全部字段互相一致。
// 若锁失效导致撕裂读，各字段会来自不同代号，断言立即失败。
// 配合 ThreadSanitizer 可从内存层面确证无数据竞争。

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "store/weather_store.h"
#include "test_framework.h"

namespace {

// 生成足够长的代号，确保 std::string 走堆分配（绕过 SSO），
// 使"指针级的撕裂读"成为可观测的真实风险。
std::string MakeTag(int generation) {
  return "gen-" + std::to_string(generation) + std::string(96, 'x');
}

// 把 WeatherNow 的全部字段设为同一代号
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

// 检查快照的全部字段来自同一个代号
bool AllFieldsConsistent(const weather::WeatherNow& now, std::string& first) {
  first = now.temp;
  return now.feelsLike == first && now.icon == first && now.text == first &&
         now.windDir == first && now.windScale == first &&
         now.humidity == first && now.vis == first;
}

}  // namespace

// ---------------- 基本行为 ----------------

TEST_CASE(Store_初始状态为空且序号为0) {
  store::WeatherStore store;
  const store::WeatherSnapshot snap = store.Snapshot();

  CHECK_EQ(snap.sequence, 0u);
  CHECK(snap.now.temp.empty());
  CHECK(snap.now_state.status == weather::SyncStatus::kIdle);
  CHECK(snap.daily_state.status == weather::SyncStatus::kIdle);
  CHECK(snap.air_quality_state.status == weather::SyncStatus::kIdle);
}

TEST_CASE(Store_写入后可读回且序号自增) {
  store::WeatherStore store;

  store.SetCurrentConditions(MakeNow("first"));
  uint32_t seq1 = store.Sequence();
  CHECK(seq1 > 0);

  store.SetCurrentConditions(MakeNow("second"));
  uint32_t seq2 = store.Sequence();
  CHECK(seq2 > seq1);

  const store::WeatherSnapshot snap = store.Snapshot();
  CHECK_EQ(snap.now.temp, std::string("second"));
  CHECK_EQ(snap.sequence, seq2);
}

TEST_CASE(Store_快照是独立拷贝_后续写入不影响已取出的快照) {
  store::WeatherStore store;
  store.SetCurrentConditions(MakeNow("original"));

  const store::WeatherSnapshot snap = store.Snapshot();
  CHECK_EQ(snap.now.temp, std::string("original"));

  // 再写一次，已取出的快照必须保持原值
  store.SetCurrentConditions(MakeNow("modified"));

  CHECK_EQ(snap.now.temp, std::string("original"));
  CHECK_EQ(snap.now.text, std::string("original"));
  // 而新取的快照是新值
  CHECK_EQ(store.Snapshot().now.temp, std::string("modified"));
}

TEST_CASE(Store_七日预报按值存取_不暴露内部指针) {
  store::WeatherStore store;

  weather::DailyForecast daily{};
  daily[0].fxDate = "2026-01-01";
  daily[0].textDay = "晴";
  daily[6].fxDate = "2026-01-07";
  store.SetDailyForecast(daily);

  const store::WeatherSnapshot snap = store.Snapshot();
  CHECK_EQ(snap.daily[0].fxDate, std::string("2026-01-01"));
  CHECK_EQ(snap.daily[6].fxDate, std::string("2026-01-07"));

  // 修改本地变量不应影响 store 内已存的数据
  daily[0].fxDate = "被篡改";
  CHECK_EQ(store.Snapshot().daily[0].fxDate, std::string("2026-01-01"));
}

// ---------------- 状态机 ----------------

TEST_CASE(Store_状态流转_syncing到ready) {
  store::WeatherStore store;

  store.MarkSyncing(weather::DataSource::kCurrentConditions);
  CHECK(store.Snapshot().now_state.status == weather::SyncStatus::kSyncing);

  store.MarkReady(weather::DataSource::kCurrentConditions, 1234);
  const store::WeatherSnapshot snap = store.Snapshot();
  CHECK(snap.now_state.status == weather::SyncStatus::kReady);
  CHECK_EQ(snap.now_state.updated_at_ms, 1234ull);
  CHECK(snap.now_state.message.empty());
}

TEST_CASE(Store_状态流转_syncing到failed带原因) {
  store::WeatherStore store;

  store.MarkSyncing(weather::DataSource::kDailyForecast);
  store.MarkFailed(weather::DataSource::kDailyForecast, "api_code_not_ok: 204",
                   999);

  const store::WeatherSnapshot snap = store.Snapshot();
  CHECK(snap.daily_state.status == weather::SyncStatus::kFailed);
  CHECK_EQ(snap.daily_state.message, std::string("api_code_not_ok: 204"));
  CHECK_EQ(snap.daily_state.updated_at_ms, 999ull);
}

TEST_CASE(Store_各数据源状态相互独立) {
  store::WeatherStore store;

  store.MarkReady(weather::DataSource::kCurrentConditions, 1);
  store.MarkFailed(weather::DataSource::kAirQuality, "boom", 2);
  store.MarkSyncing(weather::DataSource::kDailyForecast);

  const store::WeatherSnapshot snap = store.Snapshot();
  CHECK(snap.now_state.status == weather::SyncStatus::kReady);
  CHECK(snap.air_quality_state.status == weather::SyncStatus::kFailed);
  CHECK(snap.daily_state.status == weather::SyncStatus::kSyncing);
  // 未触碰的数据源保持 idle
  CHECK(snap.city_state.status == weather::SyncStatus::kIdle);
}

TEST_CASE(Store_写入城市查询) {
  store::WeatherStore store;

  weather::CityLookup city;
  city.name = "余杭";
  city.id = "101210101";
  city.lat = "30.42";
  city.lon = "120.30";
  store.SetCityLookup(city);
  store.MarkReady(weather::DataSource::kCityLookup, 7);

  const store::WeatherSnapshot snap = store.Snapshot();
  CHECK_EQ(snap.city.name, std::string("余杭"));
  CHECK_EQ(snap.city.id, std::string("101210101"));
  CHECK(snap.city_state.status == weather::SyncStatus::kReady);
}

// ---------------- 并发不变式压力测试 ----------------

TEST_CASE(Store_并发读写压力_快照始终自洽) {
  store::WeatherStore store;
  store.SetCurrentConditions(MakeNow(MakeTag(0)));

  constexpr int kWriterThreads = 4;
  constexpr int kReaderThreads = 4;
  constexpr int kIterations = 20000;

  std::atomic<bool> stop{false};
  std::atomic<int> mismatches{0};
  std::atomic<int> reads{0};

  std::vector<std::thread> writers;
  for (int t = 0; t < kWriterThreads; ++t) {
    writers.emplace_back([&store, &stop, t]() {
      for (int i = 0; i < kIterations && !stop.load(); ++i) {
        // 代号里混入线程号，避免不同写者写出相同内容而掩盖问题
        store.SetCurrentConditions(MakeNow(MakeTag(t * 1000000 + i)));
      }
    });
  }

  std::vector<std::thread> readers;
  for (int t = 0; t < kReaderThreads; ++t) {
    readers.emplace_back([&store, &stop, &mismatches, &reads]() {
      while (!stop.load()) {
        const store::WeatherSnapshot snap = store.Snapshot();
        std::string first;
        if (!AllFieldsConsistent(snap.now, first)) {
          mismatches.fetch_add(1);
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

  std::printf("       读取 %d 次，撕裂 %d 次\n", reads.load(),
              mismatches.load());
  // 加锁实现下撕裂必须为 0
  CHECK_EQ(mismatches.load(), 0);
  CHECK(reads.load() > 0);
}

TEST_CASE(Store_并发状态更新_序号单调递增) {
  store::WeatherStore store;

  constexpr int kThreads = 4;
  constexpr int kIterations = 5000;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&store, t]() {
      for (int i = 0; i < kIterations; ++i) {
        store.MarkSyncing(static_cast<weather::DataSource>(t % 4));
        store.MarkReady(static_cast<weather::DataSource>(t % 4),
                        static_cast<uint64_t>(i));
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }

  // 每线程 2 次写入，序号必须正好等于总写入次数（无丢失更新）
  CHECK_EQ(store.Sequence(), static_cast<uint32_t>(kThreads * kIterations * 2));
}
