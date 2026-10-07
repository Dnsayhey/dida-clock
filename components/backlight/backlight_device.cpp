#include "backlight/backlight_device.h"

#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_log.h>

namespace backlight {
namespace {
constexpr char kTag[] = "backlight";

// ESP32-C3 只有低速模式（高速模式仅存在于 ESP32）
constexpr ledc_mode_t kSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;

ledc_channel_config_t g_channel = {};
}  // namespace

bool BacklightDevice::Begin(gpio_num_t pin) {
  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode = kSpeedMode;
  timer_cfg.duty_resolution = LEDC_TIMER_8_BIT;
  timer_cfg.timer_num = kTimer;
  timer_cfg.freq_hz = kPwmFreqHz;
  timer_cfg.clk_cfg = LEDC_AUTO_CLK;
  esp_err_t err = ledc_timer_config(&timer_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "ledc_timer_config 失败: %s", esp_err_to_name(err));
    return false;
  }

  ledc_channel_config_t channel_cfg = {};
  channel_cfg.gpio_num = pin;
  channel_cfg.speed_mode = kSpeedMode;
  channel_cfg.channel = kChannel;
  channel_cfg.timer_sel = kTimer;
  channel_cfg.duty = 0;  // 初始熄灭，由 SetDuty 点亮
  channel_cfg.hpoint = 0;
  err = ledc_channel_config(&channel_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "ledc_channel_config 失败: %s", esp_err_to_name(err));
    return false;
  }
  g_channel = channel_cfg;

  initialized_ = true;
  ESP_LOGI(kTag, "背光就绪: %d Hz / %d 位 / GPIO %d", kPwmFreqHz,
           kPwmResolutionBits, static_cast<int>(pin));
  return true;
}

void BacklightDevice::SetDuty(uint8_t duty) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) {
    return;
  }
  duty_ = duty;
  ledc_set_duty(kSpeedMode, kChannel, duty);
  ledc_update_duty(kSpeedMode, kChannel);
}

}  // namespace backlight
