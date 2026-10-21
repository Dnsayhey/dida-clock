// "自动"主题的纯逻辑测试。
//
// 这一档的难点全在**降级链**上：什么时候有数据、什么时候没有、没有时退到哪。
// 输入全部显式传入（见 app/auto_theme.h），所以四层都能在这里钉死 ——
// 真机上很难凑出"未配网 + 光敏坏掉"这种组合，但它们是必须定义的行为。

#include <optional>

#include "app/auto_theme.h"
#include "test_framework.h"

using app::AutoThemeInput;
using app::ParseHhMm;
using app::ResolveAutoThemeDark;
using app::ShouldUseDarkBySun;

namespace {

// ParseHhMm 返回 optional，而 CHECK_EQ 需要可流式输出的类型（optional 不是），
// 因此统一走这里：先确认有值，再比值。
void CheckHhMm(const char* text, int expected_minutes) {
  const std::optional<int> got = ParseHhMm(text);
  CHECK(got.has_value());
  if (got.has_value()) {
    CHECK_EQ(*got, expected_minutes);
  }
}

void CheckRejected(const char* text) { CHECK(!ParseHhMm(text).has_value()); }

}  // namespace

TEST_CASE(ParseHhMm_接受两种真实格式) {
  // 和风天气的 sunrise / sunset
  CheckHhMm("05:52", 5 * 60 + 52);
  CheckHhMm("17:49", 17 * 60 + 49);
  // net::ReadLocalClock() 的 time（带秒）
  CheckHhMm("14:26:31", 14 * 60 + 26);
  // 边界
  CheckHhMm("00:00", 0);
  CheckHhMm("23:59", 23 * 60 + 59);
}

TEST_CASE(ParseHhMm_拒绝无法解析的输入) {
  CheckRejected("");      // 空串：SNTP 未对时时 ReadLocalClock 就是这个
  CheckRejected("--");    // 极昼极夜时服务端可能给的占位符
  CheckRejected("5:52");  // 位数不对
  CheckRejected("05:5");
  CheckRejected("05:52:3");
  CheckRejected("05-52");     // 分隔符错
  CheckRejected("aa:bb");     // 非数字
  CheckRejected("25:00");     // 时越界
  CheckRejected("12:60");     // 分越界
  CheckRejected("14:26:xx");  // 秒不是数字
  CheckRejected("14:26:3x");
  CheckRejected("14:26:31:00");
}

TEST_CASE(ShouldUseDarkBySun_白天与夜里含跨零点) {
  const int sunrise = 5 * 60 + 52;  // 05:52
  const int sunset = 17 * 60 + 49;  // 17:49

  CHECK(!ShouldUseDarkBySun(12 * 60, sunrise, sunset));     // 正午
  CHECK(!ShouldUseDarkBySun(sunrise, sunrise, sunset));     // 日出那一刻算白天
  CHECK(!ShouldUseDarkBySun(sunset - 1, sunrise, sunset));  // 日落前一分钟
  CHECK(ShouldUseDarkBySun(sunset, sunrise, sunset));  // 日落那一刻起算夜里
  CHECK(ShouldUseDarkBySun(23 * 60 + 30, sunrise, sunset));  // 跨零点
  CHECK(ShouldUseDarkBySun(3 * 60, sunrise, sunset));        // 凌晨
  CHECK(ShouldUseDarkBySun(sunrise - 1, sunrise, sunset));   // 日出前一分钟
}

TEST_CASE(AutoTheme_有日出日落时按日出日落) {
  AutoThemeInput in;
  in.sunrise_minutes = 5 * 60 + 52;
  in.sunset_minutes = 17 * 60 + 49;

  in.now_minutes = 12 * 60;
  CHECK(!ResolveAutoThemeDark(in));
  in.now_minutes = 20 * 60;
  CHECK(ResolveAutoThemeDark(in));
}

// 这一层不是罕见分支：天气数据在内存里（store 不落盘），**每次开机**都会先
// 经过它，直到首次同步完成。
TEST_CASE(AutoTheme_时间可用但无数据时走固定表) {
  AutoThemeInput in;
  in.now_minutes = 12 * 60;
  CHECK(!ResolveAutoThemeDark(in));  // 白天

  in.now_minutes = 18 * 60 + 59;
  CHECK(!ResolveAutoThemeDark(in));  // 固定表从 19:00 起才算夜里
  in.now_minutes = 19 * 60;
  CHECK(ResolveAutoThemeDark(in));
  in.now_minutes = 23 * 60;
  CHECK(ResolveAutoThemeDark(in));

  in.now_minutes = 6 * 60 + 59;
  CHECK(ResolveAutoThemeDark(in));  // 跨零点
  in.now_minutes = 7 * 60;
  CHECK(!ResolveAutoThemeDark(in));  // 固定表到 07:00 结束
}

// 未配网 / SNTP 未成功 / 时间格式异常：不只是没有日出日落，**连"现在几点"
// 都不知道**，所以按时间的策略一律不成立，只能看环境光。
TEST_CASE(AutoTheme_时间不可用时看环境光) {
  AutoThemeInput in;
  in.sunrise_minutes = 5 * 60;
  in.sunset_minutes = 18 * 60;

  in.ambient_brightness = 20;  // 暗房
  CHECK(ResolveAutoThemeDark(in));
  in.ambient_brightness = 200;  // 明亮的房间
  CHECK(!ResolveAutoThemeDark(in));

  in.ambient_brightness = app::kAmbientDarkBrightness - 1;
  CHECK(ResolveAutoThemeDark(in));
  in.ambient_brightness = app::kAmbientDarkBrightness;
  CHECK(!ResolveAutoThemeDark(in));
}

// 最后一层：光敏也读不到（传感器故障），也必须给出确定行为 ——
// 用浅色保证界面可读，与 kMinAutoDuty"极暗环境也不许黑屏"同一思路。
TEST_CASE(AutoTheme_光敏也读不到时用浅色) {
  AutoThemeInput in;
  in.ambient_brightness = std::nullopt;
  CHECK(!ResolveAutoThemeDark(in));
}

TEST_CASE(AutoTheme_异常的日出日落视为无数据) {
  AutoThemeInput in;
  in.now_minutes = 20 * 60;

  in.sunrise_minutes = 12 * 60;  // 相等属于异常数据，不能当成全天白天
  in.sunset_minutes = 12 * 60;
  CHECK(ResolveAutoThemeDark(in));  // 落到固定表 -> 夜里

  in.sunrise_minutes = 12 * 60;  // 只有日出、没有日落
  in.sunset_minutes = std::nullopt;
  in.now_minutes = 12 * 60;
  CHECK(!ResolveAutoThemeDark(in));  // 落到固定表 -> 白天

  in.sunrise_minutes = 18 * 60;  // 反向数据：日出晚于日落
  in.sunset_minutes = 6 * 60;
  in.now_minutes = 12 * 60;
  CHECK(!ResolveAutoThemeDark(in));  // 仍按固定表 -> 白天
  in.now_minutes = 20 * 60;
  CHECK(ResolveAutoThemeDark(in));  // 固定表 -> 夜里
}
