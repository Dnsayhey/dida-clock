#pragma once

#include <string>

namespace weather {

// 天气现象的分类名（最多 2 字）。
//
// 为什么需要分类：页面用一个大字做视觉锚点，而官方 text 的长短差异极大
// （"晴" 1 字 ↔ "雷阵雨伴有冰雹" 7 字），固定字号下放不下。分类名把
// 它们归成 9 类，同时页面另用一行小字显示**完整**的 text，所以不丢信息。
//
// 参数是**和风天气的 icon 代码**而不是 text：
//   * icon 是数字代码，与 lang 无关 —— 切换语言不会让分类失效
//   * text 随 lang 变化，用它做映射会在改语言时静默失效
//
// 映射依据官方 weather-conditions 表。有三处**不能想当然**，否则会把天气画错：
//   1. icon 900 是"热"，不是"晴"
//   2. 未知代码不能兜底成"晴" —— 901"冷"与 999"未知"都会被错误地画成太阳
//   3. 官方表里没有 150-153 这类夜间码，不要按"夜间=某种天气"去猜
//
// 对无法归类的代码返回**空串**，由调用方决定回退显示（例如直接显示
// 完整 text）。不猜、不兜底成某个具体天气，是这里唯一诚实的选择。
//
// 返回值指向静态存储，调用方不需要释放。
const char* WeatherCategoryName(const std::string& icon);

// 分类名是否是我们已知的 9 类之一。方便调用方判断是否需要回退。
bool IsKnownWeatherCategory(const char* category);

}  // namespace weather
