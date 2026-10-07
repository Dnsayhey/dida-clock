#include "weather/weather_category.h"

#include <cstdlib>
#include <initializer_list>
#include <string>

namespace weather {
namespace {

// 分类名。**必须是单字** —— 它显示为 44px 的大字，旁边紧挨着温度：
//
//   大字 x=14，温度 x=72，两个字就是 88px，会压住温度 30px。
//   而左侧信息块总宽只有 160px（右侧 80px 留给太空人），
//   两个字加温度是 14+88+63=165px，**物理上放不下**。
//
// 精确的说法由下方 16px 的官方完整名（"多云"/"雷阵雨伴有冰雹"）给出，
// 大字只承担"一眼看出大类"的职责。
//
// 字体（dida_cn_44）正是按这 9 个字生成的，新增分类必须同步更新
// scripts/gen_fonts.py 里的 WEATHER_SHORT。
constexpr char kClear[] = "晴";
constexpr char kCloudy[] = "云";  // 多云 / 少云 / 晴间多云
constexpr char kOvercast[] = "阴";
constexpr char kRain[] = "雨";
constexpr char kThunder[] = "雷";
constexpr char kHail[] = "雹";  // 冰雹 / 雷阵雨伴有冰雹
constexpr char kSnow[] = "雪";
constexpr char kFog[] = "雾";
constexpr char kDust[] = "沙";  // 扬沙 / 浮尘 / 沙尘暴

// 把 icon 字符串解析成整数。非数字或空串返回 -1。
int ParseIcon(const std::string& icon) {
  if (icon.empty() || icon.size() > 4) {
    return -1;
  }
  for (char c : icon) {
    if (c < '0' || c > '9') {
      return -1;
    }
  }
  return std::atoi(icon.c_str());
}

// 判断是否落在闭区间 [lo, hi]。
bool InRange(int v, int lo, int hi) { return v >= lo && v <= hi; }

}  // namespace

const char* WeatherCategoryName(const std::string& icon) {
  const int code = ParseIcon(icon);
  if (code < 0) {
    return "";
  }

  // 顺序有讲究：先判断更具体的子类，再判断大类。
  // 官方表里 302/303（雷阵雨）与 304（雷阵雨伴有冰雹）都落在 300-399 的
  // "雨"区间内，必须先于它匹配；同理 503-508（沙尘）落在 500-515 的
  // "雾"区间内。
  if (InRange(code, 400, 499)) {
    return kSnow;  // 400-410 各种雪 + 499 泛指
  }
  if (code == 302 || code == 303) {
    return kThunder;
  }
  if (InRange(code, 503, 508)) {
    return kDust;  // 503 扬沙 / 504 浮尘 / 507 沙尘暴 / 508 强沙尘暴
  }
  if (InRange(code, 500, 502) || InRange(code, 509, 515)) {
    return kFog;  // 薄雾/雾/霾 + 浓雾/强浓雾/各种霾
  }
  if (code == 304) {
    return kHail;
  }
  // 300-399 整段都归"雨"。官方表只列到 318 与 399，但这段区间在语义上就是
  // 降水 —— 官方明确说过天气现象会持续增补，按区间判比按枚举判更抗变化。
  // 400-499 同理（见上面那条），两处保持一致的取舍。
  if (InRange(code, 300, 399)) {
    return kRain;
  }
  if (code == 104) {
    return kOvercast;
  }
  if (code == 100 || code == 150) {
    return kClear;
  }
  // 夜间码 151-153 官方表里没有，但旧版接口会返回。按白天对应关系归类，
  // 属于对上游变化的防御；若某天确认不再出现，删掉这一段即可。
  if (InRange(code, 151, 153)) {
    return kCloudy;
  }
  if (InRange(code, 101, 103)) {
    return kCloudy;  // 101 多云 / 102 少云 / 103 晴间多云
  }

  // 900 热 / 901 冷 / 999 未知，以及任何官方表外的代码，都不归入 9 类。
  // 它们必须靠完整 text 表达（"热"/"冷"/"未知"），把大字塞成"晴"会骗人。
  return "";
}

bool IsKnownWeatherCategory(const char* category) {
  if (category == nullptr || category[0] == '\0') {
    return false;
  }
  // **必须比完整的 UTF-8 串，不能比首字节。**
  // 汉字首字节只区分 0xE4..0xE9 这 6 个区段，大量不同的字会撞在同一个
  // 首字节上 —— 例如 "冷"(E5 86 B7) 与 "多云" 的 "多"(E5 A4 9A) 首字节
  // 都是 0xE5，"未知" 的 "未"(E6 9C AA) 与 "晴"(E6 99 B4) 都是 0xE6。
  // 用首字节比会把"冷"和"未知"误判成已知分类。
  for (const char* known : {kClear, kCloudy, kOvercast, kRain, kThunder, kHail,
                            kSnow, kFog, kDust}) {
    if (std::string(category) == known) {
      return true;
    }
  }
  return false;
}

}  // namespace weather
