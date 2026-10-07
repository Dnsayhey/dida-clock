#pragma once

#include <lvgl.h>

// 太空人动画（10 帧，56x56，1 位掩码）。
//
// 数据由 scripts/gen_astronaut.py 生成，源码素材在
// components/assets/astronaut_src/（保留原始素材，保证可复现）。
//
// 为什么是 1 位掩码：素材是黑底白色线稿，不是照片。用 RGB565 存 10 帧要
// 63 KB，明暗两套 125 KB；按 1 位存只要 3.8 KB（省 16 倍）。且两套主题的素材只是同一张
// 图的反转 —— A1 + recolor 一套就能覆盖两个主题。
//
// **上色方式**：A1 只带透明度不带颜色，LVGL 绘制时取
// `sup.alpha_color = draw_dsc->recolor`，所以用
// `lv_obj_set_style_image_recolor()` 染色。深色主题画白、浅色主题画黑。
extern "C" const lv_image_dsc_t* const dida_astro_frames[];
extern "C" const unsigned dida_astro_frame_count;

namespace assets {

// 帧数。用来对播放索引取模。
inline unsigned AstronautFrameCount() { return dida_astro_frame_count; }

// 取第 index 帧（越界时取模回绕）。
inline const lv_image_dsc_t* AstronautFrame(unsigned index) {
  if (dida_astro_frame_count == 0) {
    return nullptr;
  }
  return dida_astro_frames[index % dida_astro_frame_count];
}

}  // namespace assets
