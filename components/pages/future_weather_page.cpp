#include "pages/future_weather_page.h"

#include <string>
#include <utility>

#include "assets/fonts.h"
#include "pages/page_layout.h"
#include "pages/ui_theme.h"

namespace pages {
namespace {

constexpr int kFirstRowY = 92;
constexpr int kRowStep = 28;
// 四列的横向位置与槽宽。宽度全部来自 scripts/measure_text.py 的**实测值**
// （它复现 LVGL 的算法：逐字形 adv_w + kerning 后各自四舍五入到整像素）。
//
//   "08-08"（最宽的日期）  48px     "周一"（最宽星期） 32px
//   "中到大雪"            64px     "08/09℃"（最宽温度）63px
//
// 定宽的两条硬性要求：
//   1. **按最宽的那个日期，不能按当天的**。"1" 只有 6px，而 "0"/"8"/"9"
//      有 10~11px，不同日期宽度能差 13px —— "10-10" 是 40px 而 "10-09"
//      是 44px，按前者定 42px 槽就会让后者换行。必须按 "08-08" 这种两个月
//      都由宽数字组成的极端情况定宽。
//   2. **必须计入 kerning 与逐字形取整**。只累加 adv_w 会少算 2~5px；
//      用 scripts/measure_text.py 算，它复现了 LVGL 的实际算法。
//
// 硬约束：官方 55 种天气现象里，最长的是"大暴雨到特大暴雨"（8 个字 = 128px），
// 而本列只有 66px。**4 个字以内能完整显示（52/55 = 95%）**，再长的 3 种
// （6/7/8 字）会被省略号截断 —— 它们都极少出现。
// 要连这 3 种也完整显示，必须去掉一列（周几，或把温度改成只显示最高温）：
// 这是 240px 屏宽的取舍，不是调参数能解决的。
constexpr int kDateX = 8;
constexpr int kDateWidth = 50;  // 最宽 48px，留 2px
constexpr int kWeekdayX = 58;
constexpr int kWeekdayWidth = 34;  // 最宽 32px，留 2px
constexpr int kWeatherX = 92;
constexpr int kWeatherWidth = 66;  // 装 4 个汉字（64px），留 2px
constexpr int kTempWidth = 66;     // 最宽 63px，留 3px
constexpr int kTempX = kScreenWidth - 14 - kTempWidth;  // 右边缘固定在 226
constexpr int kStatusY = 284;

lv_obj_t* MakeCell(lv_obj_t* parent, int x, int y, int width, bool right) {
  lv_obj_t* label = CreateBodyLabel(parent, x, y, width);
  if (right) {
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
  }
  return label;
}

}  // namespace

FutureWeatherPage::FutureWeatherPage(DataSource source)
    : Page("future_weather"), source_(std::move(source)) {}

void FutureWeatherPage::Create() {
  lv_obj_t* root = CreateFullScreenRoot();

  title_label_ = CreatePageTitle(root, "7日预报");
  divider_ = CreateDivider(root);

  date_labels_.reserve(kMaxRows);
  weekday_labels_.reserve(kMaxRows);
  weather_labels_.reserve(kMaxRows);
  temp_labels_.reserve(kMaxRows);
  for (int i = 0; i < kMaxRows; ++i) {
    const int y = kFirstRowY + i * kRowStep;
    // 四列各成一个 label：日期与星期用灰色、天气按天气上色、温度用正文色。
    // 拼成一行文本既做不到分段着色，也没法让星期不受日期数字宽度的影响。
    //
    // 日期、星期、天气都**左对齐**：日期左对齐更符合阅读习惯，而星期固定
    // 在自己的 x 上，所以日期是 "10-10" 还是 "10-11" 都不会让星期移位。
    date_labels_.push_back(MakeCell(root, kDateX, y, kDateWidth, false));
    weekday_labels_.push_back(
        MakeCell(root, kWeekdayX, y, kWeekdayWidth, false));
    // 天气列**右对齐**：它的长度差异最大（1~8 个字），左对齐时短文本后面
    // 会留出一大片空白（这正是实机反馈的"天气和温度中间空白很多"）。
    // 右对齐让所有行的天气都紧贴温度列，长文本自然向左延伸。
    lv_obj_t* weather = MakeCell(root, kWeatherX, y, kWeatherWidth, true);
    // 超长时**滚动**而不是截断。
    // SCROLL_CIRCULAR 只在文字放不下时才滚 —— 所以 4 个字以内（占 95%）
    // 是静态的，只有"大暴雨到特大暴雨"这类 6~8 字的极少数才滚动起来，
    // 既没有截断丢信息，也不会让整页都在动。
    lv_label_set_long_mode(weather, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    weather_labels_.push_back(weather);
    // 只有温度右对齐 —— 数字右对齐才好看，也让各行的 ℃ 竖直对齐。
    temp_labels_.push_back(MakeCell(root, kTempX, y, kTempWidth, true));
  }

  status_label_ = CreateCenteredLabel(root, kStatusY, kScreenWidth);

  ApplyColors();
  Refresh();
}

void FutureWeatherPage::ApplyColors() {
  ApplySkeletonColors(title_label_, divider_);
  // 日期与星期用**正文色**，不是灰色。
  //
  // 按 §17.3 的标准（"不读它就无法理解这一屏"）它们是主体：预报列表
  // 没有日期就不知道哪一行是哪天。层次由**天气颜色**承担，不必压暗日期。
  for (lv_obj_t* label : date_labels_) {
    ApplyTextRole(label, TextRole::kText);
  }
  for (lv_obj_t* label : weekday_labels_) {
    ApplyTextRole(label, TextRole::kText);
  }
  for (lv_obj_t* label : temp_labels_) {
    ApplyTextRole(label, TextRole::kText);
  }
  ApplyTextRole(status_label_, TextRole::kDanger);
  // 天气文字的颜色取决于数据，由 Refresh() 设置。
}

void FutureWeatherPage::OnThemeChanged(app::ThemeMode mode) {
  ApplyThemeToContainer(Root(), mode);
  ApplyColors();
}

void FutureWeatherPage::OnEnter() {
  if (timer_ == nullptr) {
    timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
          static_cast<FutureWeatherPage*>(lv_timer_get_user_data(timer))
              ->Refresh();
        },
        kRefreshIntervalMs, this);
  }
  Refresh();
}

void FutureWeatherPage::OnLeave() {
  if (timer_ != nullptr) {
    lv_timer_delete(timer_);
    timer_ = nullptr;
  }
}

void FutureWeatherPage::Refresh() {
  if (source_ == nullptr || date_labels_.empty()) {
    return;
  }
  const app::ForecastViewData data = source_();

  for (int i = 0; i < kMaxRows; ++i) {
    const bool has_row = static_cast<std::size_t>(i) < data.rows.size();
    lv_obj_t* cells[] = {date_labels_[i], weekday_labels_[i],
                         weather_labels_[i], temp_labels_[i]};
    for (lv_obj_t* cell : cells) {
      if (has_row) {
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (!has_row) {
      continue;
    }
    const app::ForecastRow& row = data.rows[i];
    lv_label_set_text(date_labels_[i], row.date.c_str());
    lv_label_set_text(weekday_labels_[i], row.weekday.c_str());
    lv_label_set_text(weather_labels_[i], row.text.c_str());
    lv_label_set_text(temp_labels_[i], row.temp_range.c_str());
    // 与实时天气页同一套配色：天气文字随天气变，两张页面观感一致。
    lv_obj_set_style_text_color(weather_labels_[i],
                                lv_color_hex(WeatherColor(row.category)), 0);
  }

  if (data.status.empty()) {
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(status_label_, data.status.c_str());
  }
}

}  // namespace pages
