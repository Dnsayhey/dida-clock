#pragma once

#include <lvgl.h>

#include <cstdint>
#include <string>

#include "app/settings_view_data.h"

namespace pages {

// 界面配色。纯数据，便于主机测试与后续统一替换。
struct UiColors {
  uint32_t background;
  uint32_t text;
  uint32_t text_dim;
  // 品牌强调色：时间冒号、温度数值。
  //
  // **不随数据变化** —— 时钟始终该是同一个时钟，把整体调性交给数据去改会
  // 让界面失去一致性。需要按天气/空气质量表达的语义交给 WeatherColor()
  // 与 AqiBadgeColor()，它们各有明确的辖域。
  uint32_t accent;
};

// 当前生效的主题。
//
// 只由 UI 任务读写（页面创建与主题切换都发生在 UI 任务），因此无需同步。
// 页面在 Create()/OnThemeChanged() 里读取它来决定配色。
void SetCurrentTheme(app::ThemeMode mode);
app::ThemeMode CurrentTheme();

// 主题配色表。
//
// 为什么放在 pages 层：配色是纯 UI 关注点，app 层不该知道具体色值。
// 主题**档位**（app::ThemeMode）属于应用设置，由 app 层定义。
UiColors ColorsForTheme(app::ThemeMode mode);

// 文字角色：页面只需要声明"这是什么用途的文字"，颜色由主题决定。
// 这样主题切换时页面只需重新调用 ApplyTextRole()，不必自己算色值。
// 全站只有主题色（黑底白字 / 白底黑字）外加一个危险提示色。
// 标题与正文同色，主次靠 kDim 区分。
//
// 注意：lv_color_hex() 接收 24 位 RGB888，不要传 16 位 RGB565 值。
enum class TextRole {
  kText,     // 正文与标题
  kDim,      // 次要说明
  kDanger,   // 危险操作提示
  kOnBadge,  // 压在彩色徽章上的文字。恒为白 —— 徽章底色都是暗色，
             // 白字在两种主题下与它都有足够对比（最低 4.8）。
             // 单独列一个角色，是为了让"徽章上的字"不必在页面里硬编码。
};

uint32_t ColorForRole(const UiColors& colors, TextRole role);

// 按**当前主题**给 label 上色。
void ApplyTextRole(lv_obj_t* label, TextRole role);

// 把配色应用到"本页的全屏容器"上。
// 不依赖 LVGL 默认主题（已被关闭以节省 Flash），因此样式必须显式设置。
void ApplyThemeToContainer(lv_obj_t* container, app::ThemeMode mode);

// ---------------- 随数据变化的颜色 ----------------
//
// 为什么需要**两套**色表：鲜艳颜色（如强调色橙 #FF8D00）在纯黑底上
// 对比度有 9.1，但在纯白底上只有 2.3 —— 远低于可读门槛。所以同一色调
// 在浅色主题下必须换成暗版。两套都按 WCAG 对比度 ≥4.5（正文级）挑选，
// 具体数值见 CONVENTIONS.md 附录 A。
//
// 注意区分**载体**：
//   * 天气用**文字色**   -> WeatherColor()
//   * 空气质量用**底色** -> AqiBadgeColor()（白字压在上面，所以底色要够暗）
// 载体不同，两个元素即便相邻也不会让人混淆"这个颜色在说什么"。

// 天气分类（weather::WeatherCategoryName() 的返回值）在当前主题下该用的
// 文字色。分类为空或不认识时返回主题正文色，不猜。
uint32_t WeatherColor(const std::string& category);

// 空气质量徽章的底色。**不区分主题** —— 徽章是深色填充 + 白字，
// 在两种主题下与背景都有足够对比。
uint32_t AqiBadgeColor(const std::string& category);

}  // namespace pages
