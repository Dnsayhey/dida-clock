#include "backlight/light_sensor.h"

#include <esp_err.h>
#include <esp_log.h>

namespace backlight {
namespace {
constexpr char kTag[] = "light_sensor";
}  // namespace

LightSensor::~LightSensor() {
  if (handle_ != nullptr) {
    adc_oneshot_del_unit(handle_);
    handle_ = nullptr;
  }
}

bool LightSensor::Begin(adc_unit_t unit, adc_channel_t channel,
                        adc_atten_t atten) {
  channel_ = channel;

  adc_oneshot_unit_init_cfg_t unit_cfg = {};
  unit_cfg.unit_id = unit;
  unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

  esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &handle_);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "adc_oneshot_new_unit 失败: %s", esp_err_to_name(err));
    handle_ = nullptr;
    return false;
  }

  adc_oneshot_chan_cfg_t chan_cfg = {};
  chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;  // 默认即 12 位 → 0..4095
  chan_cfg.atten = atten;
  err = adc_oneshot_config_channel(handle_, channel_, &chan_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "adc_oneshot_config_channel 失败: %s", esp_err_to_name(err));
    adc_oneshot_del_unit(handle_);
    handle_ = nullptr;
    return false;
  }

  ESP_LOGI(kTag, "光敏就绪: ADC%d 通道 %d", static_cast<int>(unit) + 1,
           static_cast<int>(channel_));
  return true;
}

bool LightSensor::ReadRaw(uint16_t& out_raw) const {
  if (handle_ == nullptr) {
    return false;
  }
  int raw = 0;
  if (adc_oneshot_read(handle_, channel_, &raw) != ESP_OK) {
    return false;
  }
  if (raw < 0) {
    raw = 0;
  }
  out_raw = static_cast<uint16_t>(raw);
  return true;
}

}  // namespace backlight
