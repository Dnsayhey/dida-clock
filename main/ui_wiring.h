#pragma once

// 页面装配：页面注册、切页、数据回调、按键轮询。
//
// 分层位置：本文件是 main/ 里**唯一**知道"页面对象长什么样"的地方。
// 其他模块只通过语义动作（切到某页）与纯值对象（各类 ViewData）与 UI 交互，
// 因此 startup_task / startup_flow 里不会出现页面类型或 LVGL 调用。
//
// 线程约定：RegisterPages / SwitchToPage / StartButtonPolling 都涉及 LVGL 对象。
//   * RegisterPages —— app_main 在启动期调用，调用方负责持锁；
//   * SwitchToPage  —— 自己取 LVGL 锁（可能被启动任务调用）；
//   * StartButtonPolling 与各数据回调 —— 运行在 LVGL 任务上下文。

#include <cstdint>

#include "app/page_type.h"
#include "app/settings_view_data.h"
#include "app/weather_view_data.h"

namespace dida {

// ---------------- 数据回调：页面 ← 全局状态 ----------------
//
// 页面不认识 net / store / config，装配层在这里把数据组装成纯值对象交给它们。

app::SettingsViewData BuildSettingsData();
// 时钟与天气内容分开：前者每秒都要读，后者最快 30 分钟才变一次。
// 合成一个回调会让"只为看秒"也付一次完整天气快照的代价。
app::ClockViewData BuildClockView();
// 内容版本号：判脏只需一次整数读取，不必先取快照。
uint32_t BuildContentVersion();
app::WeatherPageViewData BuildWeatherPageData();
app::ForecastViewData BuildForecastData();

// ---------------- 命令处理：UI 动作 → 状态/硬件 ----------------

// 单击：切换亮度档位。
void CycleBrightness();

// 长按：切换浅色/深色主题。
void ToggleTheme();

// 恢复出厂：清 NVS 并重启。
void DoFactoryReset();

// ---------------- 页面注册与切换 ----------------

// 页面注册：**启动期一次性完成**，之后不再变动。
// 与 AppScheduler 的 Seal 同理（CONVENTIONS §6.1）——
// 运行期往页面表里加东西，会与切页/刷新路径并发访问同一个容器。
void RegisterPages();

// 切页。改 UI 必须持 LVGL 锁，故由本函数自己取。
void SwitchToPage(app::PageType page);

// 创建按键轮询定时器。**必须在持有 LVGL 锁时调用** ——
// 定时器回调跑在 LVGL 任务里，创建也必须在该任务的上下文里完成。
void StartButtonPolling();

}  // namespace dida
