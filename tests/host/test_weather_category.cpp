// 天气分类映射的单测（主机运行）。
//
// 这段逻辑值得单独测，原因有三：
//   1. icon 900/901/999 与未知代码没有对应天气，返回空串交由调用方回退
//   2. **区间有包含关系，判断顺序错了不会报错**。302/303/304 都落在 300-399
//      里，503-508 都落在 500-515 里；顺序写反会把"雷阵雨"归成"雨"、
//      把"沙尘暴"归成"雾"，代码照样编译通过。
//   3. **输入来自网络**。icon 是字符串，必须能扛住空串、非数字、超长输入。
//
// 期望值全部来自和风天气官方 weather-conditions 表。

#include <string>

#include "test_framework.h"
#include "weather/weather_category.h"

using weather::IsKnownWeatherCategory;
using weather::WeatherCategoryName;

namespace {

// 把分类名转成 std::string，方便 CHECK_EQ 打印出可读的失败信息。
std::string Cat(const char* icon) { return WeatherCategoryName(icon); }

}  // namespace

// ---------------- 九个分类，每个都要能从官方代码命中 ----------------

TEST_CASE(WeatherCategory_晴) { CHECK_EQ(Cat("100"), std::string("晴")); }

TEST_CASE(WeatherCategory_多云含少云与晴间多云) {
  CHECK_EQ(Cat("101"), std::string("云"));  // 多云
  CHECK_EQ(Cat("102"), std::string("云"));  // 少云
  CHECK_EQ(Cat("103"), std::string("云"));  // 晴间多云
}

TEST_CASE(WeatherCategory_阴) { CHECK_EQ(Cat("104"), std::string("阴")); }

TEST_CASE(WeatherCategory_雨覆盖整个300区间) {
  CHECK_EQ(Cat("300"), std::string("雨"));  // 阵雨
  CHECK_EQ(Cat("305"), std::string("雨"));  // 官方 text 是"小雨"，但分类是"雨"
  CHECK_EQ(Cat("306"), std::string("雨"));  // 中雨
  CHECK_EQ(Cat("310"), std::string("雨"));  // 暴雨
  CHECK_EQ(Cat("313"), std::string("雨"));  // 冻雨
  CHECK_EQ(Cat("318"), std::string("雨"));  // 大暴雨到特大暴雨
  CHECK_EQ(Cat("399"), std::string("雨"));  // 泛指
}

TEST_CASE(WeatherCategory_雷阵雨被单独分出来) {
  // 302/303 落在 300-399 里，必须**先于**"雨"判断。
  // 顺序写反会让雷阵雨显示成"雨"，而且不会有任何编译或运行错误。
  CHECK_EQ(Cat("302"), std::string("雷"));
  CHECK_EQ(Cat("303"), std::string("雷"));
}

TEST_CASE(WeatherCategory_雷阵雨伴有冰雹归为冰雹) {
  // 304 同时落在 300-399（雨）里，必须先判。
  CHECK_EQ(Cat("304"), std::string("雹"));
}

TEST_CASE(WeatherCategory_雪覆盖400区间) {
  CHECK_EQ(Cat("400"), std::string("雪"));  // 小雪
  CHECK_EQ(Cat("403"), std::string("雪"));  // 暴雪
  CHECK_EQ(Cat("404"), std::string("雪"));  // 雨夹雪
  CHECK_EQ(Cat("410"), std::string("雪"));  // 大到暴雪
  CHECK_EQ(Cat("499"), std::string("雪"));  // 泛指
}

TEST_CASE(WeatherCategory_雾与霾同一类) {
  CHECK_EQ(Cat("500"), std::string("雾"));  // 薄雾
  CHECK_EQ(Cat("501"), std::string("雾"));  // 雾
  CHECK_EQ(Cat("502"), std::string("雾"));  // 霾
  CHECK_EQ(Cat("509"), std::string("雾"));  // 浓雾
  CHECK_EQ(Cat("511"), std::string("雾"));  // 中度霾
  CHECK_EQ(Cat("515"), std::string("雾"));  // 特强浓雾
}

TEST_CASE(WeatherCategory_沙尘被单独分出来) {
  // 503-508 落在 500-515 里，必须先于"雾"判断。
  CHECK_EQ(Cat("503"), std::string("沙"));  // 扬沙
  CHECK_EQ(Cat("504"), std::string("沙"));  // 浮尘
  CHECK_EQ(Cat("507"), std::string("沙"));  // text"沙尘暴" -> 分类"沙尘"
  CHECK_EQ(Cat("508"), std::string("沙"));  // text"强沙尘暴" -> 分类"沙尘"
}

// ---------------- 表外代码不得兜底成"晴" ----------------

TEST_CASE(WeatherCategory_900是热不是晴) {
  // 官方表里 900 是"热"，不是"晴"，
  // 归到任何具体天气都是编造，必须返回空串交由调用方回退显示完整 text。
  CHECK_EQ(Cat("900"), std::string(""));
}

TEST_CASE(WeatherCategory_901是冷不是晴) {
  CHECK_EQ(Cat("901"), std::string(""));
}

TEST_CASE(WeatherCategory_999是未知不兜底成晴) {
  // 把未知代码兜底成"晴"是最坏的一种"猜"：
  // 用户看到一个大太阳，而实际数据是"未知"。
  CHECK_EQ(Cat("999"), std::string(""));
}

TEST_CASE(WeatherCategory_表外代码一律不猜) {
  CHECK_EQ(Cat("105"), std::string(""));
  CHECK_EQ(Cat("200"), std::string(""));
  CHECK_EQ(Cat("298"), std::string(""));
  CHECK_EQ(Cat("516"), std::string(""));
  CHECK_EQ(Cat("517"), std::string(""));
  CHECK_EQ(Cat("902"), std::string(""));
}

TEST_CASE(WeatherCategory_区间内的未列举代码按语义归类) {
  // 官方把 399/499 标为"雨"/"雪"的泛指，说明 300-399 与 400-499 是
  // **语义区间**而不是离散枚举；官方也明确说过天气现象会持续增补。
  // 所以区间内的新代码应归入该区间，当成未知反而会漏掉真实的天气变化。
  CHECK_EQ(Cat("319"), std::string("雨"));
  CHECK_EQ(Cat("350"), std::string("雨"));
  CHECK_EQ(Cat("398"), std::string("雨"));
  CHECK_EQ(Cat("411"), std::string("雪"));
  CHECK_EQ(Cat("450"), std::string("雪"));
  CHECK_EQ(Cat("498"), std::string("雪"));
}

// ---------------- 夜间码 ----------------

TEST_CASE(WeatherCategory_夜间码归到白天对应类) {
  // 官方表里没有 150-153，但旧版接口会返回。按白天对应关系归类，
  // 属于对上游变化的防御；删掉这段实现时本用例也要一并删。
  CHECK_EQ(Cat("150"), std::string("晴"));
  CHECK_EQ(Cat("151"), std::string("云"));
  CHECK_EQ(Cat("152"), std::string("云"));
  CHECK_EQ(Cat("153"), std::string("云"));
}

// ---------------- 坏输入 ----------------

TEST_CASE(WeatherCategory_坏输入不崩且返回空) {
  CHECK_EQ(Cat(""), std::string(""));
  CHECK_EQ(Cat("abc"), std::string(""));
  CHECK_EQ(Cat("10a"), std::string(""));
  CHECK_EQ(Cat(" 100"), std::string(""));  // 前导空格
  CHECK_EQ(Cat("100 "), std::string(""));  // 尾随空格
  CHECK_EQ(Cat("+100"), std::string(""));  // 带符号
  CHECK_EQ(Cat("-100"), std::string(""));
  CHECK_EQ(Cat("99999"), std::string(""));  // 超长（>4 位）
  CHECK_EQ(Cat("1.00"), std::string(""));
}

TEST_CASE(WeatherCategory_三位以内数字都能解析) {
  // 官方代码最长 3 位（如 515）。前导零不该影响结果。
  CHECK_EQ(Cat("100"), std::string("晴"));
  CHECK_EQ(Cat("0100"), std::string("晴"));  // 4 位但数值仍是 100
}

// ---------------- IsKnownWeatherCategory ----------------

TEST_CASE(WeatherCategory_已知分类判定) {
  for (const char* c : {"晴", "云", "阴", "雨", "雷", "雹", "雪", "雾", "沙"}) {
    CHECK(IsKnownWeatherCategory(c));
  }
}

TEST_CASE(WeatherCategory_空与未知分类判定为假) {
  CHECK(!IsKnownWeatherCategory(""));
  CHECK(!IsKnownWeatherCategory(nullptr));
  CHECK(!IsKnownWeatherCategory("热"));
  CHECK(!IsKnownWeatherCategory("冷"));
  CHECK(!IsKnownWeatherCategory("未知"));
}
