#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace app {

// "HH:MM" 或 "HH:MM:SS" -> 一天中的分钟数。
//
// 两种格式都是真实存在的：和风天气的 sunrise/sunset 是 "05:52"，
// net::ReadLocalClock() 的 time 是 "14:26:31"。
//
// 无法解析（"--"、空串、位数不对、越界）时返回 nullopt —— 调用方据此**降级**，
// 不要把它当成 0 点：那会让主题在错误的时刻翻转，而且完全没有迹象。
std::optional<int> ParseHhMm(std::string_view text);

// now 是否落在 [sunrise, sunset) **之外**，即"该用深色"。
// 跨零点安全：日落之后的那段时间一直延续到次日日出。
// 前提：sunrise < sunset（异常数据由调用方在降级链里拦掉）。
bool ShouldUseDarkBySun(int now_minutes, int sunrise_minutes,
                        int sunset_minutes);

// "自动"档的输入。**全部显式传入、不读全局** —— 于是下面四层降级
// 每一层都能在主机上单独测出来。
struct AutoThemeInput {
  // 本地时间（一天中的分钟数）。nullopt 表示时间不可用：
  // 未对时，或时间字符串无法解析。此时自动档退到环境光。
  std::optional<int> now_minutes;
  std::optional<int> sunrise_minutes;  // 今天的；拿不到就用最近一次的
  std::optional<int> sunset_minutes;
  std::optional<uint8_t> ambient_brightness;  // 0..255，越大越亮
};

// 自动档此刻该不该用深色。降级顺序：
//   1. 时间可用 + 有日出日落           -> 按日出日落
//   2. 时间可用 + 没数据               -> 固定表（19:00 起深色）
//   3. 时间不可用（未配网/未对时/解析失败）-> 看环境光："不知道几点，但知道黑不黑"
//   4. 光敏也读不到                    -> 浅色，保证可读
//
// 第 2 层不是罕见分支：天气数据在内存里（store 不落盘），选了"自动"的设备
// **每次开机**都会先经过它，直到首次同步完成。第 4 层与 kMinAutoDuty
// "极暗环境也不许黑屏"是同一个思路：降级到最后也要保证界面可读。
bool ResolveAutoThemeDark(const AutoThemeInput& in);

// 固定表兜底：这段时间视为"夜里"（只在天亮/天黑数据缺失时使用）。
inline constexpr int kFallbackDarkFromMinutes = 19 * 60;
inline constexpr int kFallbackDarkToMinutes = 7 * 60;

// 第 3 层的判据：环境亮度低于它就认为"环境是暗的"（约 31%）。
inline constexpr uint8_t kAmbientDarkBrightness = 80;

}  // namespace app
