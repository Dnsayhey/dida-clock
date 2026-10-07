#include "scheduler/app_scheduler.h"

#include <algorithm>
#include <cstdio>

namespace scheduler {

AppScheduler::AppScheduler(MonotonicClock clock) : clock_(std::move(clock)) {}

bool AppScheduler::HasTask(const std::string& name) const {
  return std::any_of(
      tasks_.begin(), tasks_.end(),
      [&name](const ScheduledTask& t) { return t.name == name; });
}

bool AppScheduler::Add(const std::string& name, uint32_t interval_ms,
                       std::function<void()> callback, bool run_now) {
  // 运行期注册是要禁止的核心情形 —— 直接拒绝，不静默接受。
  if (sealed_.load()) {
    rejected_registrations_.fetch_add(1);
    return false;
  }
  if (name.empty()) {
    // 无名任务在诊断时毫无用处，直接拒绝
    rejected_registrations_.fetch_add(1);
    return false;
  }
  if (interval_ms == 0 || !callback) {
    rejected_registrations_.fetch_add(1);
    return false;
  }
  if (HasTask(name)) {
    rejected_registrations_.fetch_add(1);
    return false;
  }

  ScheduledTask task;
  task.name = name;
  task.interval_ms = interval_ms;
  task.callback = std::move(callback);

  const uint64_t now = clock_ ? clock_() : 0;
  if (run_now) {
    // 先执行再入表：这样回调里若试图再注册，仍处于未封存但已明确的状态，
    // 且首次计时从"执行完"开始，避免首次到期时间被提前消耗。
    task.callback();
    task.last_run_ms = clock_ ? clock_() : now;
  } else {
    task.last_run_ms = now;
  }

  // 先预留再插入，避免回调（上面 run_now 分支）与本次插入交错时
  // 出现引用失效 —— 虽然封存前是单线程，这里仍保持保守写法。
  tasks_.reserve(tasks_.size() + 1);
  tasks_.push_back(std::move(task));
  return true;
}

void AppScheduler::Seal() { sealed_.store(true); }

void AppScheduler::RunDue() {
  const uint64_t now = clock_ ? clock_() : 0;

  for (ScheduledTask& task : tasks_) {
    // 无符号相减：回绕安全（两个操作数都是无符号，跨回绕点算出的间隔依然正确）
    const uint64_t elapsed = now - task.last_run_ms;
    if (elapsed < task.interval_ms) {
      continue;
    }
    // 先记录起点再执行：即使回调较慢，也不会把下一次到期时间推后。
    task.last_run_ms = now;
    task.callback();
  }
}

std::string AppScheduler::Describe() const {
  const uint64_t now = clock_ ? clock_() : 0;
  std::string out;
  for (const ScheduledTask& task : tasks_) {
    const uint64_t elapsed = now - task.last_run_ms;
    const uint64_t remaining =
        elapsed >= task.interval_ms ? 0 : task.interval_ms - elapsed;
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer),
                  "%s: interval=%u ms, next in=%llu ms\n", task.name.c_str(),
                  static_cast<unsigned>(task.interval_ms),
                  static_cast<unsigned long long>(remaining));
    out += buffer;
  }
  if (sealed_.load()) {
    out += "[sealed]\n";
  }
  return out;
}

}  // namespace scheduler
