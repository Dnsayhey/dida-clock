#include "app/auto_theme.h"

namespace app {
namespace {

// now 是否落在可能跨零点的区间 [from, to) 内。
bool InWindow(int now, int from, int to) {
  if (from <= to) {
    return now >= from && now < to;
  }
  // 跨零点：例如 [19:00, 07:00)
  return now >= from || now < to;
}

}  // namespace

std::optional<int> ParseHhMm(std::string_view text) {
  if (text.size() != 5 && text.size() != 8) {
    return std::nullopt;
  }
  if (text[2] != ':' || (text.size() == 8 && text[5] != ':')) {
    return std::nullopt;
  }
  const auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
  if (!is_digit(text[0]) || !is_digit(text[1]) || !is_digit(text[3]) ||
      !is_digit(text[4])) {
    return std::nullopt;
  }
  // 秒也必须是数字；目前只用到时分，但格式必须完整合法。
  if (text.size() == 8 && (!is_digit(text[6]) || !is_digit(text[7]))) {
    return std::nullopt;
  }
  const int hours = (text[0] - '0') * 10 + (text[1] - '0');
  const int minutes = (text[3] - '0') * 10 + (text[4] - '0');
  if (hours > 23 || minutes > 59) {
    return std::nullopt;
  }
  return hours * 60 + minutes;
}

bool ShouldUseDarkBySun(int now_minutes, int sunrise_minutes,
                        int sunset_minutes) {
  // 白天是 [日出, 日落)，深色是剩下的部分：日落 -> 次日日出（跨零点）。
  return !InWindow(now_minutes, sunrise_minutes, sunset_minutes);
}

bool ResolveAutoThemeDark(const AutoThemeInput& in) {
  // 只有 sunrise < sunset 才是可信的白天区间；相等、反向或缺失都视为无数据。
  const bool has_sun = in.sunrise_minutes.has_value() &&
                       in.sunset_minutes.has_value() &&
                       *in.sunrise_minutes < *in.sunset_minutes;

  if (in.now_minutes.has_value()) {
    if (has_sun) {
      return ShouldUseDarkBySun(*in.now_minutes, *in.sunrise_minutes,
                                *in.sunset_minutes);
    }
    return InWindow(*in.now_minutes, kFallbackDarkFromMinutes,
                    kFallbackDarkToMinutes);
  }
  if (in.ambient_brightness.has_value()) {
    return *in.ambient_brightness < kAmbientDarkBrightness;
  }
  return false;
}

}  // namespace app
