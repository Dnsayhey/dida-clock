#pragma once

#include <lvgl.h>

// 字库声明。
//
// 这里**不用** LVGL 内置的 lv_font_montserrat_*，原因见 scripts/gen_fonts.py：
// 内置字体是 157 字形的拉丁扩展集，本界面用不到那么多；而中文必须自定义
// 子集（全量 CJK 有 1 MB 量级，4 MB flash 放不下）。
//
// 字库由 scripts/gen_fonts.py 生成 —— 它会扫描源码的字符串字面量，
// 自动把界面实际用到的字都纳入子集，另加一份地级市字集（charset/city.txt）。
// **改完文案要重新生成**，否则缺字在屏幕上就是空白；
// scripts/check_fonts.py 会在约定检查里守住这一点。

// ---- 原始字体（名称即用途，勿在页面里直接用，请用下面的语义别名）----
extern "C" const lv_font_t dida_num_64;  // 时间（等宽数字）
extern "C" const lv_font_t dida_num_26;  // 温度
extern "C" const lv_font_t dida_num_22;  // 秒（等宽数字）
extern "C" const lv_font_t dida_cn_28;   // 页面标题
extern "C" const lv_font_t dida_cn_44;   // 天气大字
extern "C" const lv_font_t dida_cn_16;   // 正文（中文 + 拉丁混排）

namespace assets {

// ---- 语义别名 ----
//
// 页面只该引用这些名字。这样调整字号阶梯时不需要动页面代码，
// 也避免"这个页面用了 26 号而那个页面用了 25 号"的漂移。

// 时间：70px 大号数字
inline constexpr const lv_font_t* kFontTime = &dida_num_64;
// 秒：24px，与时间顶部对齐
inline constexpr const lv_font_t* kFontSeconds = &dida_num_22;
// 温度：26px
inline constexpr const lv_font_t* kFontTemperature = &dida_num_26;
// 页面标题：28px，只含各页标题用到的字
inline constexpr const lv_font_t* kFontTitle = &dida_cn_28;
// 天气大字：44px，只含 9 类短名
inline constexpr const lv_font_t* kFontWeather = &dida_cn_44;
// 正文：16px，日期/城市/滚动信息条/天气全名/状态文字都用它
inline constexpr const lv_font_t* kFontBody = &dida_cn_16;

}  // namespace assets
