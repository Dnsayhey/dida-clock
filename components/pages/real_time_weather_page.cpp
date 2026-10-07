#include "pages/real_time_weather_page.h"

#include <cstdio>
#include <string>
#include <utility>

#include "assets/astronaut.h"
#include "assets/fonts.h"
#include "pages/ui_theme.h"

namespace pages {
namespace {

// ---- 版面常量（240x320）----
//
// 左侧信息块的纵向范围是 kBlockTop..kBlockBottom，太空人按这段的中心
// 垂直居中 —— 太空人若贴底摆放，就与左侧信息失去对齐关系，看着会"掉下去"。
constexpr int kScreenWidth = 240;

constexpr int kDateY = 28;

// 时间与秒**顶部对齐**：两个字体都是 lv_font_conv 生成的紧致文字盒
// （dida_num_64 的 line_height 是 47、字形 ofs_y 在 -1..0；dida_num_22 是
// 16 / 0）。字形顶几乎就贴着盒顶，所以对齐盒顶等于对齐数字顶，不需要补偿。
// 让时间在「日期下方 .. 分割线上方」之间**垂直居中**。
//   日期标签 28..46（高 18）      分割线 160
//   时间标签高 47 → 居中后 80..127，上留 34px、下留 33px
// 取 66 时上留 20px、下留 47px，肉眼看就是"时间偏上、下面空太多"。
constexpr int kTimeY = 80;
constexpr int kTimeX = 8;
// 秒的 x 由时分的宽度推出来，不是写死的数：
//   时分 64px 等宽 → 4x43 + 冒号 15 = 187px 恒定
//   秒 22px 等宽   → 两位 30px
//   8 + 187 + 8(间距) = 203，右边缘 233/240，余 7px
// 时分等宽是关键：宽度恒定，这个 x 才不会在某些时刻被压到。
constexpr int kSecondsX = kTimeX + 187 + 8;

constexpr int kDividerY = 160;
constexpr int kDividerX = 14;
constexpr int kDividerWidth = 212;
constexpr int kTickerY = 174;

// 左侧信息块。也是太空人垂直居中的基准。
constexpr int kBlockTop = 214;
constexpr int kBlockBottom = 282;
constexpr int kAstroSize = 56;

constexpr int kWeatherX = 14;
constexpr int kTemperatureX = 72;
constexpr int kTextX = 16;
constexpr int kCityX = 74;
constexpr int kTextSlotWidth = 48;
constexpr int kBadgeX = 126;
constexpr int kBadgeW = 38;
constexpr int kBadgeH = 21;

constexpr int kAstroX = 176;
constexpr int kAstroY = (kBlockTop + kBlockBottom) / 2 - kAstroSize / 2;

// 秒的颜色。红色保留 —— 它是屏幕上唯一固定不随主题变的强调色，
// 能立刻把"秒"从"时:分"里区分出来。
constexpr uint32_t kSecondsColor = 0xFF3B30;

// 太空人换帧的节拍。每 tick 换一帧，tick 周期见 kAstroIntervalMs。
constexpr unsigned kAstroFramesPerTick = 1;

lv_obj_t* MakeLabel(lv_obj_t* parent, const lv_font_t* font, int x, int y,
                    int width, int height) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_pos(label, x, y);
  if (width > 0) {
    lv_obj_set_width(label, width);
  }
  if (height > 0) {
    lv_obj_set_height(label, height);
  }
  return label;
}

// 把 RGB888 拼成 LVGL recolor 的起始标记 "#RRGGBB "。
std::string RecolorStart(uint32_t rgb) {
  char buf[16];
  // 显式转成 unsigned：在 riscv32 上 uint32_t 是 long unsigned int，
  // 直接喂给 %X 会触发 -Wformat。
  std::snprintf(buf, sizeof(buf), "#%06X ",
                static_cast<unsigned>(rgb & 0xFFFFFFu));
  return buf;
}

}  // namespace

RealTimeWeatherPage::RealTimeWeatherPage(ClockSource clock,
                                         ContentVersion content_version,
                                         DataSource content)
    : Page("real_time_weather"),
      clock_(std::move(clock)),
      content_version_(std::move(content_version)),
      content_(std::move(content)) {}

void RealTimeWeatherPage::Create() {
  lv_obj_t* root = CreateFullScreenRoot();

  // ---- 顶部：日期 + 星期 ----
  date_label_ = MakeLabel(root, assets::kFontBody, 0, kDateY, kScreenWidth, 0);
  lv_obj_set_style_text_align(date_label_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(date_label_, LV_LABEL_LONG_MODE_DOTS);

  // ---- 中部：时间 + 秒 ----
  time_label_ = MakeLabel(root, assets::kFontTime, kTimeX, kTimeY, 0, 0);
  seconds_label_ =
      MakeLabel(root, assets::kFontSeconds, kSecondsX, kTimeY, 0, 0);
  lv_obj_set_style_text_color(seconds_label_, lv_color_hex(kSecondsColor), 0);

  // 冒号用强调色。LVGL 的 recolor 转义可以只给一段文字上色，
  // 不必把时:分拆成三个 label。
  lv_label_set_recolor(time_label_, true);

  // ---- 分隔线 ----
  divider_ = lv_obj_create(root);
  lv_obj_remove_style_all(divider_);
  lv_obj_set_size(divider_, kDividerWidth, 1);
  lv_obj_set_pos(divider_, kDividerX, kDividerY);
  lv_obj_set_style_bg_opa(divider_, LV_OPA_COVER, 0);

  // ---- 滚动信息条 ----
  ticker_label_ = MakeLabel(root, assets::kFontBody, kDividerX, kTickerY,
                            kDividerWidth, 18);
  // 循环滚动：四段拼起来必然超出 212px，而这一条本来就是"信息流"。
  // 城市名与天气完整名**刻意不滚动** —— 静态信息一直在动会显得吵，
  // 而且滚动时任一时刻只看得到一半。
  lv_label_set_long_mode(ticker_label_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

  // ---- 底部：天气 / 温度 / 完整名 / 城市 / 空气质量 ----
  weather_label_ =
      MakeLabel(root, assets::kFontWeather, kWeatherX, kBlockTop, 0, 0);
  temperature_label_ = MakeLabel(root, assets::kFontTemperature, kTemperatureX,
                                 kBlockTop + 8, 0, 0);
  weather_text_label_ = MakeLabel(root, assets::kFontBody, kTextX,
                                  kBlockTop + 48, kTextSlotWidth, 18);
  // 同 7 日预报：放得下就静态，放不下才滚，不截断。
  lv_label_set_long_mode(weather_text_label_,
                         LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

  // 城市用固定槽宽：槽位固定，版面就不会随城市名长短漂移。
  city_label_ = MakeLabel(root, assets::kFontBody, kCityX, kBlockTop + 48,
                          kTextSlotWidth, 18);
  lv_label_set_long_mode(city_label_, LV_LABEL_LONG_MODE_DOTS);

  aqi_badge_ = lv_obj_create(root);
  lv_obj_remove_style_all(aqi_badge_);
  lv_obj_set_size(aqi_badge_, kBadgeW, kBadgeH);
  lv_obj_set_pos(aqi_badge_, kBadgeX, kBlockTop + 47);
  lv_obj_set_style_radius(aqi_badge_, 5, 0);
  lv_obj_set_style_bg_opa(aqi_badge_, LV_OPA_COVER, 0);
  aqi_label_ = lv_label_create(aqi_badge_);
  lv_obj_set_style_text_font(aqi_label_, assets::kFontBody, 0);
  lv_obj_center(aqi_label_);

  // ---- 太空人 ----
  astro_image_ = lv_image_create(root);
  lv_image_set_src(astro_image_, assets::AstronautFrame(0));
  lv_obj_set_pos(astro_image_, kAstroX, kAstroY);

  // ---- 未就绪提示 ----
  status_label_ =
      MakeLabel(root, assets::kFontBody, kTextX, kBlockTop, kDividerWidth, 0);
  lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

  // 创建即渲染一次：计时器还没建，RefreshClock() 只写 label、不重排周期。
  Refresh();
}

void RealTimeWeatherPage::ApplyColors(const app::WeatherPageViewData& data) {
  const UiColors colors = ColorsForTheme(CurrentTheme());

  // 日期用**正文色**。
  //
  // 这一页的主次由**字号**承担（时间 70px vs 日期 18px，差 4 倍），不需要
  // 再靠压暗来退居其次 —— 和设置页标签是同一个道理（CONVENTIONS §17.3）：
  // 层次靠字号与颜色，不靠亮度差。日期是真实信息，灰掉只会让它难读。
  ApplyTextRole(date_label_, TextRole::kText);
  ApplyTextRole(time_label_, TextRole::kText);
  // 滚动条是"可读可不读"的补充信息（风向/体感/能见度），天气全名也只是
  // 上方那个大字的精确化说明 —— 这两处用灰色是对的。
  ApplyTextRole(ticker_label_, TextRole::kDim);
  ApplyTextRole(weather_text_label_, TextRole::kDim);
  ApplyTextRole(city_label_, TextRole::kText);
  ApplyTextRole(aqi_label_, TextRole::kOnBadge);
  ApplyTextRole(status_label_, TextRole::kDanger);

  // 温度数值用**品牌强调色**，不随天气或温度变化。
  // 它紧挨着空气质量徽章，若也按冷热变红/蓝，同一屏就会出现两套
  // "红=严重"的语义，人会搞混。
  lv_obj_set_style_text_color(temperature_label_, lv_color_hex(colors.accent),
                              0);
  lv_obj_set_style_bg_color(divider_, lv_color_hex(colors.text), 0);

  // 太空人是 A1 掩码（只有透明度、没有颜色），靠 recolor 上色 ——
  // 深色主题画白、浅色主题画黑，**一套素材覆盖两个主题**。
  lv_obj_set_style_image_recolor(astro_image_, lv_color_hex(colors.text), 0);
  lv_obj_set_style_image_recolor_opa(astro_image_, LV_OPA_COVER, 0);

  // 天气大字与徽章的颜色取决于数据。放在这里、且只由内容变化驱动：
  // 设置样式同样是**无条件** invalidate，每秒写一次就等于每秒重绘这两个区域。
  lv_obj_set_style_text_color(
      weather_label_, lv_color_hex(WeatherColor(data.weather_category)), 0);
  if (!data.aqi_category.empty()) {
    lv_obj_set_style_bg_color(
        aqi_badge_, lv_color_hex(AqiBadgeColor(data.aqi_category)), 0);
  }
}

void RealTimeWeatherPage::OnThemeChanged(app::ThemeMode mode) {
  ApplyThemeToContainer(Root(), mode);
  // 冒号的强调色是**内嵌在文本里**的 recolor 转义，不是样式属性 ——
  // 换了主题必须把文本重新拼一次，先把缓存清掉。
  shown_hhmm_.clear();
  RefreshClock();
  RefreshContent(CurrentContentVersion());
}

void RealTimeWeatherPage::OnEnter() {
  // **定时器只配给真正周期性的东西**（CONVENTIONS §18.1）。
  //
  // 时钟 1s：回调只写真正变了的那一个 label（秒每秒、分每分钟、日期每天）。
  // 太空人 35ms：只换一张 56x56 的图。
  //
  // 两者周期差一个数量级，必须分开：合并成一个定时器后，时钟会被迫跟着
  // 35ms 的节拍重写，LVGL 任务长期繁忙，10ms 的按键轮询被反复推迟，
  // 100~200ms 的短按整个漏采 —— 表现为"双击偶尔变成单击"。
  //
  // 天气内容**不在这里**：它是非周期事件，由 OnTick() 里的版本号判脏驱动。
  if (refresh_timer_ == nullptr) {
    refresh_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
          static_cast<RealTimeWeatherPage*>(lv_timer_get_user_data(timer))
              ->OnTick();
        },
        kSecondMs, this);
  }
  if (astro_timer_ == nullptr) {
    astro_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
          static_cast<RealTimeWeatherPage*>(lv_timer_get_user_data(timer))
              ->AdvanceAstronaut();
        },
        kAstroIntervalMs, this);
  }
  // 进入即刷一次；RefreshClock() 会把刷新周期对齐到秒边界。
  Refresh();
}

void RealTimeWeatherPage::OnLeave() {
  // 离开时两个都要停：否则隐藏页面还在以约 30fps 换图，白占 LVGL 任务。
  if (refresh_timer_ != nullptr) {
    lv_timer_delete(refresh_timer_);
    refresh_timer_ = nullptr;
  }
  if (astro_timer_ != nullptr) {
    lv_timer_delete(astro_timer_);
    astro_timer_ = nullptr;
  }
}

void RealTimeWeatherPage::AdvanceAstronaut() {
  if (astro_image_ == nullptr) {
    return;
  }
  astro_frame_ += kAstroFramesPerTick;
  lv_image_set_src(astro_image_, assets::AstronautFrame(astro_frame_));
}

uint32_t RealTimeWeatherPage::CurrentContentVersion() const {
  return content_version_ != nullptr ? content_version_() : 0;
}

void RealTimeWeatherPage::Refresh() {
  // PageManager 驱动的整页刷新（进入页面、主题/亮度变化）：
  // 调用点期待"整页都是最新的"，所以内容也强制重建一次。
  RefreshClock();
  RefreshContent(CurrentContentVersion());
}

void RealTimeWeatherPage::OnTick() {
  // 顺序要紧：RefreshClock() 依据**当前**墙上时钟重排下一个周期，
  // 排在内容重建之前，才不会被重建的耗时推到秒边界之后。
  RefreshClock();

  if (content_version_ == nullptr) {
    return;  // 没有版本号就无从判脏；内容只由 Refresh() 驱动
  }
  const uint32_t version = content_version_();
  if (version != last_content_version_) {
    RefreshContent(version);
  }
}

void RealTimeWeatherPage::RefreshClock() {
  if (clock_ == nullptr || time_label_ == nullptr) {
    return;
  }
  const app::ClockViewData clock = clock_();

  // ---- 日期与星期（每天变一次）----
  //
  // 分开缓存而不是缓存拼接结果：拼接成 "10月07日  周二" 是 18 字节，
  // 超过 SSO 阈值，每秒为此做一次堆分配不值得 —— 这两段本身都装得下。
  if (clock.date != shown_date_ || clock.weekday != shown_weekday_) {
    const std::string date_line =
        clock.weekday.empty() ? clock.date : clock.date + "  " + clock.weekday;
    lv_label_set_text(date_label_, date_line.c_str());
    shown_date_ = clock.date;
    shown_weekday_ = clock.weekday;
  }

  // ---- 时:分（每分钟变一次）：只把冒号染成强调色 ----
  // recolor 的语法是 "#RRGGBB 文字#"，结束标记是单个 '#'。
  if (clock.hhmm != shown_hhmm_) {
    if (clock.hhmm.size() == 5) {
      const std::string colored =
          clock.hhmm.substr(0, 2) +
          RecolorStart(ColorsForTheme(CurrentTheme()).accent) + ":#" +
          clock.hhmm.substr(3, 2);
      lv_label_set_text(time_label_, colored.c_str());
    } else {
      lv_label_set_text(time_label_, clock.hhmm.c_str());
    }
    shown_hhmm_ = clock.hhmm;
  }

  // ---- 秒（每秒变一次）----
  if (clock.seconds != shown_seconds_) {
    lv_label_set_text(seconds_label_, clock.seconds.c_str());
    shown_seconds_ = clock.seconds;
  }

  // ---- 把下一次触发对齐到墙上时钟的秒边界 ----
  //
  // 周期固定 1000ms 的话，相位就由**创建定时器那一刻**决定，秒位会在一个
  // 随机的时刻跳字，且每次进入页面都不同 —— 与手机对表时一眼可见。
  // 按当前秒内位置重算后，相位恒定贴在秒边界上，且不累积。
  // 算式本身是纯逻辑，见 app::ClockRefreshPeriodMs()（含 lv_tick 量化带来的
  // "可能早一个网格"那一层说明）。
  if (refresh_timer_ == nullptr) {
    return;
  }
  // 时间未同步时没有可对齐的目标，退回每秒一次：三个 label 都不会变，
  // 这一轮实际不产生任何 LVGL 写入。
  const uint32_t period =
      clock.hhmm.empty()
          ? kSecondMs
          : app::ClockRefreshPeriodMs(clock.ms_into_second, kTickGridMs);
  lv_timer_set_period(refresh_timer_, period);
  // 让周期从**这次取样**起算，而不是从回调入口起算（lv_timer_exec 在调用
  // 回调前就把 last_run 设成了入口时刻）。不重置的话，相位会比预期早
  // 取样耗时那么多；而在回调之外调用本函数时（Refresh / 主题切换），
  // last_run 可能是近一秒前的旧值，会让下一次触发立刻到期。
  lv_timer_reset(refresh_timer_);
}

void RealTimeWeatherPage::RefreshContent(uint32_t version) {
  if (content_ == nullptr || time_label_ == nullptr) {
    return;
  }
  // **先记版本号，再取数据**：反过来的话，若两次调用之间恰好有一次写入，就会把
  // "新版本"标记为已渲染、实际渲染的却是旧数据 —— 那次更新永远丢掉。按此顺序
  // 最坏只是多重渲一次（下个周期发现版本又变了）。
  last_content_version_ = version;

  // 每次都是一份新构造的值对象，页面不持有任何共享状态
  const app::WeatherPageViewData data = content_();

  ApplyColors(data);

  // ---- 滚动信息条 ----
  lv_label_set_text(ticker_label_, data.ticker.c_str());

  // ---- 天气大字 ----
  // 分类为空（官方表外的 icon 代码，如 900 热 / 999 未知）时**不猜**，
  // 直接把官方完整 text 放进大字位 —— 宁可字长一点，也不谎报天气。
  const bool has_category = !data.weather_category.empty();
  lv_label_set_text(weather_label_, has_category ? data.weather_category.c_str()
                                                 : data.weather_text.c_str());
  lv_label_set_text(weather_text_label_, data.weather_text.c_str());

  lv_label_set_text(temperature_label_, data.temperature.c_str());
  lv_label_set_text(city_label_, data.city.c_str());

  // ---- 空气质量徽章 ----
  if (data.aqi_category.empty()) {
    lv_obj_add_flag(aqi_badge_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(aqi_badge_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(aqi_label_, data.aqi_short.c_str());
  }

  // ---- 未就绪提示 ----
  // 有数据时把提示藏起来（而不是留一行空白），并把天气区显示出来；
  // 没数据时反过来，让提示占据天气区的位置，避免两段文字叠在一起。
  const bool ready = data.status.empty();
  if (ready) {
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(weather_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(temperature_label_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(status_label_, data.status.c_str());
    lv_obj_add_flag(weather_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(temperature_label_, LV_OBJ_FLAG_HIDDEN);
  }
}

}  // namespace pages
