#pragma once

// 启动流程里的"强制门户"与"进入正式页面"。
//
// 分层位置：本模块知道**是否处于配网模式**与**该进哪个页面**，
// 但**不碰任何 UI** —— 实际切页通过注入的回调完成（见 NavigationRequest），
// 所以 startup_flow.cpp 里不会出现 LVGL 调用或页面类型。

#include "app/page_type.h"
#include "storage/device_config.h"

namespace dida {

// 请求切到某个页面。实现由装配层注入 —— app_main 传 ui_wiring 的 SwitchToPage。
//
// 用函数指针而不是 std::function：它是个纯函数、无状态，函数指针足够且更省。
using NavigationRequest = void (*)(app::PageType page);

// 启动强制门户并把热点信息发布给配网页。
// 表单保存后由门户层触发重启，因此本函数之后不会有"配网完成继续启动"的路径。
bool StartPortal();

// 配网表单落库。
//
// **由 esp_http_server 自己的任务调用**，不是启动任务。因此这里刻意只动 NVS：
//   * 不去改全局 g_device_config —— 那是启动任务持有的状态，跨任务写会竞争；
//   * 不碰任何 UI。
// 保存完成后门户会在启动任务里执行重启，新配置在下次启动时生效。
bool SaveProvisionedConfig(const config::DeviceConfig& incoming);

// 启动流程收尾：非配网路径先把启动状态置为就绪，再请求进入对应页面。
//
// 两种模式的落点不同是有意的：配网模式下"就绪"无从谈起，启动状态应停在
// kWaitingForConfig，让 INIT 页继续显示等待配网。
void EnterInitialPage(bool portal_mode, NavigationRequest on_navigate);

}  // namespace dida
