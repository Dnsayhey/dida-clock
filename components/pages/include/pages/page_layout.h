#pragma once

#include <lvgl.h>

namespace pages {

// 各页面共用的版面骨架与尺寸。
//
// 实时天气页是**环境显示**，不需要标题；其余页面是**功能页**，需要标题来定位，
// 所以它们统一用下面这套骨架：
//
//        标题                 28px 居中
//     ──────────────          1px 分隔线
//        内容                 16px，从 kContentTop 开始
//
// 把这些常量与构造函数放在一处，是为了避免"这个页面标题在 y=24、那个在 y=26"
// 这类漂移 —— 五个页面各写一份坐标，改一次就要改五处。

constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 320;

constexpr int kTitleY = 26;
constexpr int kDividerY = 74;
constexpr int kContentTop = 92;
constexpr int kMarginX = 16;

// 页面标题：28px 居中，超出时省略。
lv_obj_t* CreatePageTitle(lv_obj_t* parent, const char* text);

// 分隔线：1px 横线，颜色跟随主题正文色。
lv_obj_t* CreateDivider(lv_obj_t* parent);

// 正文标签：16px，左对齐。width 为 0 表示不限制（随文本）。
lv_obj_t* CreateBodyLabel(lv_obj_t* parent, int x, int y, int width);

// 居中正文标签：16px，占满屏宽。
lv_obj_t* CreateCenteredLabel(lv_obj_t* parent, int y, int width);

// 一行"标签 —— 值"：标签左对齐、值右对齐。
// 返回值的 label 供调用方设色与改文本。
void CreateLabelValueRow(lv_obj_t* parent, int y, const char* label_text,
                         lv_obj_t** out_label, lv_obj_t** out_value);

// 给本页的标题与分隔线上色（跟随主题）。
void ApplySkeletonColors(lv_obj_t* title, lv_obj_t* divider);

}  // namespace pages
