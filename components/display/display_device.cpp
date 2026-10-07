#include "display/display_device.h"

#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_st7789.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>

#include "board/board_config.h"

namespace display {
namespace {

constexpr char kTag[] = "display";

constexpr spi_host_device_t kSpiHost = SPI2_HOST;
constexpr int kTransQueueDepth = 10;
constexpr int kLcdCmdBits = 8;
constexpr int kLcdParamBits = 8;

// LVGL 绘制缓冲大小（像素）：屏幕的 1/10，即 240x32，RGB565 + DMA 下约 15 KB。
// 再小会让一次刷屏被切成很多块，SPI 传输与 LVGL 锁的开销明显上升；
// 再大则要和 Wi-Fi / mbedTLS 抢本就紧张的内部 DRAM（ESP32-C3 无 PSRAM）。
// 单缓冲、无 PSRAM，且该缓冲常驻不释放 —— 改大之前先确认 DRAM 余量。
constexpr uint32_t kDrawBufferPixels =
    static_cast<uint32_t>(board::kLcdHRes) * board::kLcdVRes / 10;

}  // namespace

esp_err_t DisplayDevice::Begin() {
  ESP_LOGI(kTag, "init SPI bus (SCLK=%d MOSI=%d)",
           static_cast<int>(board::kLcdSclk),
           static_cast<int>(board::kLcdMosi));

  spi_bus_config_t bus_cfg = {};
  bus_cfg.sclk_io_num = board::kLcdSclk;
  bus_cfg.mosi_io_num = board::kLcdMosi;
  bus_cfg.miso_io_num = -1;  // 无触摸、不回读
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;
  bus_cfg.max_transfer_sz = board::kLcdHRes * 80 * sizeof(uint16_t);
  esp_err_t err = spi_bus_initialize(kSpiHost, &bus_cfg, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "spi_bus_initialize failed: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(kTag, "install panel IO");
  esp_lcd_panel_io_spi_config_t io_cfg = {};
  io_cfg.cs_gpio_num = board::kLcdCs;
  io_cfg.dc_gpio_num = board::kLcdDc;
  io_cfg.spi_mode = 0;
  io_cfg.pclk_hz = board::kLcdPixelClockHz;
  io_cfg.trans_queue_depth = kTransQueueDepth;
  io_cfg.lcd_cmd_bits = kLcdCmdBits;
  io_cfg.lcd_param_bits = kLcdParamBits;
  err = esp_lcd_new_panel_io_spi(
      static_cast<esp_lcd_spi_bus_handle_t>(kSpiHost), &io_cfg, &io_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(kTag, "install ST7789 panel driver (%dx%d, BGR=%d, invert=%d)",
           board::kLcdHRes, board::kLcdVRes, board::kLcdBgrElementOrder,
           board::kLcdInvertColor);
  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = board::kLcdRst;
  panel_cfg.rgb_ele_order = board::kLcdBgrElementOrder
                                ? LCD_RGB_ELEMENT_ORDER_BGR
                                : LCD_RGB_ELEMENT_ORDER_RGB;
  panel_cfg.bits_per_pixel = 16;
  err = esp_lcd_new_panel_st7789(io_handle_, &panel_cfg, &panel_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_lcd_new_panel_st7789 failed: %s", esp_err_to_name(err));
    return err;
  }

  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle_));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle_));
  ESP_ERROR_CHECK(
      esp_lcd_panel_invert_color(panel_handle_, board::kLcdInvertColor));

  // ---- LVGL 移植层：由组件创建并拥有 LVGL 任务 ----
  ESP_LOGI(kTag, "init LVGL port (its own task will drive lv_timer_handler)");
  lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  // 页面的刷新定时器跑在 LVGL 任务里，而每次刷新都会做一次
  // WeatherStore::Snapshot()（实测 2064 字节，且是按值返回）。
  // 组件默认 7168 字节对"快照 + LVGL 文字排版"偏紧，放宽到 10240。
  port_cfg.task_stack = 10240;
  // LVGL 的 tick 周期：lv_tick 每这么多毫秒才前进一格，因此**所有 lv_timer 的
  // 到期判定都只能落在这一格的整数倍上**（1..5 的 period 实效都是 5ms）。
  //
  // 显式写出来，是因为它与页面按秒边界对齐刷新时用的网格是**同一个数**：
  //   * 实时天气页的时钟刷新按这个步进取整（pages 里的 kTickGridMs）；
  //   * 改动这里而不改那边，不会报错，只会让秒位的相位偏差变大 —— 很难查。
  // 保持 5：更细的网格意味着 esp_timer 回调频率成倍上升（1ms 就是 1000 次/秒），
  // 对"肉眼看不出偏差"这个目标没有收益。
  port_cfg.timer_period_ms = 5;
  err = lvgl_port_init(&port_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "lvgl_port_init failed: %s", esp_err_to_name(err));
    return err;
  }

  lvgl_port_display_cfg_t disp_cfg = {};
  disp_cfg.io_handle = io_handle_;
  disp_cfg.panel_handle = panel_handle_;
  disp_cfg.buffer_size = kDrawBufferPixels;
  disp_cfg.double_buffer = false;
  disp_cfg.hres = board::kLcdHRes;
  disp_cfg.vres = board::kLcdVRes;
  disp_cfg.monochrome = false;
  disp_cfg.rotation.swap_xy = false;
  disp_cfg.rotation.mirror_x = false;
  disp_cfg.rotation.mirror_y = false;
  disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
  disp_cfg.flags.buff_dma = true;      // SPI 走 DMA
  disp_cfg.flags.buff_spiram = false;  // ESP32-C3 无 PSRAM
  disp_cfg.flags.sw_rotate = false;
  // 必须开启：LVGL 的 RGB565 缓冲在小端机上按"低字节在前"存放，而 ST7789
  // 期望高字节在前。5-6-5 的字段不按字节对齐，不交换会把字段边界搅乱。
  // 与 BGR 位的组合关系见 board_config.h 里 kLcdBgrElementOrder 的对照表。
  disp_cfg.flags.swap_bytes = true;
  disp_cfg.flags.full_refresh = false;
  disp_cfg.flags.direct_mode = false;

  lv_display_ = lvgl_port_add_disp(&disp_cfg);
  if (lv_display_ == nullptr) {
    ESP_LOGE(kTag, "lvgl_port_add_disp failed");
    return ESP_FAIL;
  }

  // 面板上电（背光由 board 层单独控制）
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle_, true));

  ESP_LOGI(kTag, "display ready");
  return ESP_OK;
}

bool DisplayDevice::Lock(uint32_t timeout_ms) {
  return lvgl_port_lock(timeout_ms);
}

void DisplayDevice::Unlock() { lvgl_port_unlock(); }

}  // namespace display
