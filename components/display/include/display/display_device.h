#pragma once

#include <esp_err.h>
#include <esp_lcd_types.h>
#include <lvgl.h>

namespace display {

// 显示设备：SPI + ST7789 + LVGL 移植层。
//
// 并发约定：
//   * LVGL 任务由 esp_lvgl_port 自己创建并拥有；
//   * 任何**非 LVGL 任务**在触碰 LVGL 对象前必须 Lock()，用完 Unlock()；
//   * 无论谁改 UI，都不要在持锁期间做阻塞操作（HTTP / flash 写）。
class DisplayDevice {
 public:
  // 初始化 SPI 总线、ST7789 面板、LVGL 移植层，并注册 LVGL 显示。
  // 成功后 LVGL 任务已开始运行。
  esp_err_t Begin();

  // 线程安全地获取 LVGL 访问权。返回 false 表示超时。
  bool Lock(uint32_t timeout_ms);
  void Unlock();

  lv_display_t* lv_display() const { return lv_display_; }

 private:
  esp_lcd_panel_io_handle_t io_handle_ = nullptr;
  esp_lcd_panel_handle_t panel_handle_ = nullptr;
  lv_display_t* lv_display_ = nullptr;
};

}  // namespace display
