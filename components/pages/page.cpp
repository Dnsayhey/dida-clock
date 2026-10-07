#include "pages/page.h"

#include "pages/ui_theme.h"

namespace pages {

void Page::OnThemeChanged(app::ThemeMode mode) {
  ApplyThemeToContainer(root_, mode);
}

lv_obj_t* Page::CreateFullScreenRoot() {
  lv_obj_t* root = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(root);  // 不依赖 LVGL 默认主题
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(root, 0, 0);
  // 底色取自当前主题，不能硬编码。
  //
  // Create() 期间 OnThemeChanged() 尚未被调用（启动路径只调 SetCurrentTheme()），
  // 所以这一句是页面唯一一次上底色；硬编码会让它在换主题后仍是旧颜色。
  ApplyThemeToContainer(root, CurrentTheme());
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  SetRoot(root);
  return root;
}

}  // namespace pages
