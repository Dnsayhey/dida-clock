// 背光策略与设置文案的单测（主机运行）。
//
// 背光换算与档位解析是纯逻辑，出错时上机表现为"自动亮度反了""固定档位无效"，
// 这类问题靠肉眼调试很费时间，因此在主机上把换算表全部钉住。

#include "app/settings_view_data.h"
#include "backlight/backlight_policy.h"
#include "test_framework.h"

using backlight::BrightnessMode;
using backlight::kMinAutoDuty;
using backlight::NextMode;
using backlight::RawToBrightness;
using backlight::ResolveDuty;

// ---------------- 光敏换算 ----------------

// 取反的线性映射：ADC 读数越大越暗
TEST_CASE(Backlight_光敏换算为取反线性映射) {
  CHECK_EQ(RawToBrightness(0), 255);     // 最亮
  CHECK_EQ(RawToBrightness(4095), 0);    // 最暗
  CHECK_EQ(RawToBrightness(2047), 128);  // 中点附近
}

TEST_CASE(Backlight_光敏换算在标定区间内) {
  // 自定义标定区间：只关心 1000..3000
  CHECK_EQ(RawToBrightness(1000, 1000, 3000), 255);
  CHECK_EQ(RawToBrightness(3000, 1000, 3000), 0);
  // 区间外的读数被夹取，不应产生越界结果
  CHECK_EQ(RawToBrightness(0, 1000, 3000), 255);
  CHECK_EQ(RawToBrightness(4095, 1000, 3000), 0);
}

TEST_CASE(Backlight_标定区间退化时不崩) {
  // in_max == in_min 时不能除零
  const uint8_t value = RawToBrightness(2048, 1000, 1000);
  CHECK(value <= 255);
}

// ---------------- 档位轮换 ----------------

TEST_CASE(Backlight_档位按顺序轮换并回到自动) {
  CHECK(NextMode(BrightnessMode::kAuto) == BrightnessMode::kLevel1);
  CHECK(NextMode(BrightnessMode::kLevel1) == BrightnessMode::kLevel2);
  CHECK(NextMode(BrightnessMode::kLevel2) == BrightnessMode::kLevel3);
  CHECK(NextMode(BrightnessMode::kLevel3) == BrightnessMode::kAuto);
}

// ---------------- 档位 -> 占空比 ----------------

// 固定档位用预设值：{128, 80, 160, 255}
TEST_CASE(Backlight_固定档位用预设占空比) {
  CHECK_EQ(ResolveDuty(BrightnessMode::kLevel1, 200), 80);
  CHECK_EQ(ResolveDuty(BrightnessMode::kLevel2, 200), 160);
  CHECK_EQ(ResolveDuty(BrightnessMode::kLevel3, 200), 255);
}

// 固定档位必须**忽略**环境光
TEST_CASE(Backlight_固定档位忽略环境光) {
  for (const uint8_t ambient : {0, 64, 128, 255}) {
    CHECK_EQ(ResolveDuty(BrightnessMode::kLevel1, ambient), 80);
    CHECK_EQ(ResolveDuty(BrightnessMode::kLevel2, ambient), 160);
    CHECK_EQ(ResolveDuty(BrightnessMode::kLevel3, ambient), 255);
  }
}

// AUTO 档必须**跟随**环境光（但不低于下限，见下面那条用例）
TEST_CASE(Backlight_自动档跟随环境光) {
  // 0 会被抬到下限，所以从下限之上取值来验证"跟随"本身
  CHECK_EQ(ResolveDuty(BrightnessMode::kAuto, 77), 77);
  CHECK_EQ(ResolveDuty(BrightnessMode::kAuto, 128), 128);
  CHECK_EQ(ResolveDuty(BrightnessMode::kAuto, 255), 255);
}

// 档位表本身要钉住 —— 这些数值直接决定用户看到的亮度，改动是可见的行为变化
TEST_CASE(Backlight_档位表取值) {
  CHECK_EQ(backlight::kFixedDuty[0], 128);  // AUTO 初值
  CHECK_EQ(backlight::kFixedDuty[1], 80);
  CHECK_EQ(backlight::kFixedDuty[2], 160);
  CHECK_EQ(backlight::kFixedDuty[3], 255);
}

// ---------------- 设置页文案 ----------------

TEST_CASE(Settings_档位文案) {
  CHECK_EQ(app::FormatSettingsView(BrightnessMode::kAuto, app::ThemeMode::kDark)
               .brightness,
           std::string("自动"));
  CHECK_EQ(
      app::FormatSettingsView(BrightnessMode::kLevel1, app::ThemeMode::kDark)
          .brightness,
      std::string("低"));
  CHECK_EQ(
      app::FormatSettingsView(BrightnessMode::kLevel2, app::ThemeMode::kDark)
          .brightness,
      std::string("中"));
  CHECK_EQ(
      app::FormatSettingsView(BrightnessMode::kLevel3, app::ThemeMode::kDark)
          .brightness,
      std::string("高"));
}

TEST_CASE(Settings_主题文案) {
  const auto dark =
      app::FormatSettingsView(BrightnessMode::kAuto, app::ThemeMode::kDark);
  CHECK_EQ(dark.theme, std::string("深色"));

  const auto light =
      app::FormatSettingsView(BrightnessMode::kAuto, app::ThemeMode::kLight);
  CHECK_EQ(light.theme, std::string("浅色"));
}

TEST_CASE(Settings_主题切换是二值往返) {
  CHECK(app::NextThemeMode(app::ThemeMode::kDark) == app::ThemeMode::kLight);
  CHECK(app::NextThemeMode(app::ThemeMode::kLight) == app::ThemeMode::kDark);
}

TEST_CASE(Settings_主题切换往返回到原值) {
  const app::ThemeMode start = app::ThemeMode::kDark;
  const app::ThemeMode once = app::NextThemeMode(start);
  const app::ThemeMode twice = app::NextThemeMode(once);
  CHECK(twice == start);
}

// 自动档**绝不能把屏幕点到全黑**。
//
// 实机反馈"环境光太黑会直接黑屏"：RawToBrightness() 只做取反线性映射，
// 极暗环境下输出 0，而 0 占空比就是屏幕熄灭。屏幕一黑，用户就没法看着
// 屏幕把亮度调回来 —— 单按键设备上等于锁死。
TEST_CASE(Backlight_自动档有下限不会黑屏) {
  for (int b = 0; b <= 255; ++b) {
    const uint8_t duty =
        ResolveDuty(BrightnessMode::kAuto, static_cast<uint8_t>(b));
    CHECK(duty >= kMinAutoDuty);
  }
  // 恰好落在下限以下时被抬到下限
  CHECK_EQ(ResolveDuty(BrightnessMode::kAuto, 0), kMinAutoDuty);
  CHECK_EQ(ResolveDuty(BrightnessMode::kAuto, kMinAutoDuty - 1), kMinAutoDuty);
  // 高于下限时原样透传（下限不该改变正常区间的响应）
  CHECK_EQ(ResolveDuty(BrightnessMode::kAuto, kMinAutoDuty + 1),
           kMinAutoDuty + 1);
  CHECK_EQ(ResolveDuty(BrightnessMode::kAuto, 255), 255);
}

TEST_CASE(Backlight_固定档位都不为0) {
  for (const BrightnessMode m :
       {BrightnessMode::kLevel1, BrightnessMode::kLevel2,
        BrightnessMode::kLevel3}) {
    // 固定档位用预设值，与环境光无关，都不该是 0
    CHECK(ResolveDuty(m, 0) > 0);
    CHECK(ResolveDuty(m, 255) > 0);
  }
}
