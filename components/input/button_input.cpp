#include "input/button_input.h"

#include <esp_log.h>

namespace input {
namespace {
constexpr char kTag[] = "button";
}  // namespace

bool ButtonInput::Begin(gpio_num_t pin, ButtonTiming timing) {
  pin_ = pin;
  detector_ = ButtonEventDetector(timing);

  gpio_config_t config = {};
  config.pin_bit_mask = 1ULL << static_cast<unsigned>(pin);
  config.mode = GPIO_MODE_INPUT;
  // 上拉输入：按键另一端接地，按下读到低电平
  config.pull_up_en = GPIO_PULLUP_ENABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  // 轮询而非中断：识别状态机本来就需要时间戳轮询，用中断反而复杂
  config.intr_type = GPIO_INTR_DISABLE;

  const esp_err_t err = gpio_config(&config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "gpio_config 失败: %s", esp_err_to_name(err));
    return false;
  }

  initialized_ = true;
  ESP_LOGI(kTag, "按键初始化完成: GPIO %d（上拉，按下为低）",
           static_cast<int>(pin));
  return true;
}

ButtonEvent ButtonInput::Poll(uint32_t now_ms) {
  if (!initialized_) {
    return ButtonEvent::kNone;
  }
  const bool pressed = gpio_get_level(pin_) == 0;
  return detector_.Update(pressed, now_ms);
}

}  // namespace input
