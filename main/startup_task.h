#pragma once

// 启动任务：全部阻塞 I/O 都在这一侧。
//
// 职责不止网络：联网/门户 → 对时 → 定位 → 同步天气 → 请求进入正式页面，
// 之后转入常驻的调度循环。
//
// 分层：本模块**不直接碰 UI**。启动流程收尾时通过 NavigationRequest 回调
// 请求切页，回调由 app_main 在装配时注入（实现在 ui_wiring）。
//
// 线程约定：本任务与 LVGL 任务是两个独立调度实体 —— 网络侧在做阻塞 HTTP 时，
// 按键与界面依然即时响应。

#include "startup_flow.h"

namespace dida {

// 创建启动任务：栈 12 KB（这一侧要跑 HTTPS，mbedTLS 握手吃栈较深），优先级 4。
//
// 只创建、不等待：任务永不返回，句柄不再需要。
void StartStartupTask(NavigationRequest on_navigate);

}  // namespace dida
