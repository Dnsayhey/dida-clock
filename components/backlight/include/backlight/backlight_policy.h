#pragma once

#include <cstdint>

namespace backlight {

// 背光档位。数值顺序就是 NVS 里持久化的值（0..3），不可重排：
// 重排会让已有设备读到的档位含义整体错位。
enum class BrightnessMode {
  kAuto = 0,
  kLevel1,
  kLevel2,
  kLevel3,
  kCount,
};

const char* BrightnessModeName(BrightnessMode mode);

// 固定档位的占空比（Auto 档的数值只是初值，运行期会被光敏读数覆盖）：
//   {AUTO, LEVEL_1, LEVEL_2, LEVEL_3} = {128, 80, 160, 255}
// 这三个固定值直接决定用户看到的亮度，改动就是可见的行为变化，
// 因此由测试逐项钉住。
constexpr uint8_t kFixedDuty[static_cast<int>(BrightnessMode::kCount)] = {
    128, 80, 160, 255};

// 光敏原始 ADC 值 -> 0..255 亮度。
// 取反的线性映射：ADC 读数越大表示环境越暗，因此结果取反。
// raw 超出 [min_value, max_value] 时夹取，避免出发条越界。
uint8_t RawToBrightness(uint16_t raw, uint16_t min_value = 0,
                        uint16_t max_value = 4095);

// 档位轮换：AUTO -> L1 -> L2 -> L3 -> AUTO
BrightnessMode NextMode(BrightnessMode mode);

// AUTO 档的**占空比下限**（26/255，约 10%）。
//
// 必须有下限：极暗环境下 RawToBrightness() 会输出 0，而 0 占空比等于屏幕全黑；
// 单按键设备上屏幕一黑，用户就看不见自己在按什么，也就没法把亮度调回来。
// 26 来自实测：50（20%）偏亮、5（2%）在暗房已看不清，10% 是"暗但可读"的平衡点。
constexpr uint8_t kMinAutoDuty = 26;

// 纯策略：给定档位与当前环境亮度，决定最终 PWM 占空比。
//   * 固定档位 -> 用预设值，忽略环境光（都 > 0，不会黑屏）
//   * AUTO     -> 用环境亮度，但不低于 kMinAutoDuty
uint8_t ResolveDuty(BrightnessMode mode, uint8_t auto_brightness);

}  // namespace backlight
