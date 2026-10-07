#pragma once

// 天气数据结构。
//
// 7 日预报用 std::array **按值**持有：多任务会并发读预报，而返回内部
// 指针（裸数组 + 长度）等于把内部状态的地址交出去，读写之间必然撕裂。
// 值语义让跨任务共享只能走"快照"（§5）。

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace weather {

// 和风天气 7 日预报的固定条目数
constexpr std::size_t kDailyCount = 7;

struct CityLookup {
  // name 是**匹配到的**地点名，可能是区或县（搜"萧山"就返回"萧山"）。
  std::string name;
  // adm2 是它所属的**地级市**（搜"萧山"/"桐庐"都返回"杭州"）。
  //
  // 界面**优先显示 name 而不是 adm2** —— 理由是版面，不是语义：
  // 城市标签的槽位只有 kTextSlotWidth=48px（3 个汉字），而 adm2 有 22.4%
  // 的取值超过 3 字（"乌鲁木齐市""乌兰察布市"…）会被省略号截断，
  // name 则只有 2.4% 超宽。
  //
  // 代价是字集：name 含区县，全域 1284 个不同汉字。本项目把 省 ∪ 市 ∪ 区县
  // 合并去重成 1297 字固化为 charset/city.txt，16px 正文字体因此从 94 KB
  // 涨到 192 KB —— 固件实测 **+100,288 B（+97.9 KB）**，边际 128.2 B/字形。
  std::string adm2;
  std::string adm1;  // 省 / 直辖市
  std::string id;    // location id
  std::string lat;
  std::string lon;
};

struct WeatherNow {
  std::string temp;
  std::string feelsLike;
  std::string icon;
  std::string text;
  std::string windDir;
  std::string windScale;
  std::string humidity;
  std::string vis;
};

struct WeatherDaily {
  std::string fxDate;
  std::string sunrise;
  std::string sunset;
  std::string tempMax;
  std::string tempMin;
  std::string iconDay;
  std::string textDay;
  std::string iconNight;
  std::string textNight;
};

struct AirQualityNow {
  std::string aqi;
  std::string category;
  std::string primaryPollutant;
  std::string pm2p5;
};

using DailyForecast = std::array<WeatherDaily, kDailyCount>;

// 数据源标识：状态与数据分开记录，便于页面区分"哪一路还没好"
enum class DataSource {
  kCurrentConditions,
  kAirQuality,
  kDailyForecast,
  kCityLookup,
};

const char* DataSourceName(DataSource source);

enum class SyncStatus {
  kIdle,
  kSyncing,
  kReady,
  kFailed,
};

const char* SyncStatusName(SyncStatus status);

// 单路数据源的同步状态。
// 带 error 分级（见 weather_parser.h 的 ParseError）：网络失败、解析失败与
// "响应里根本没有数据"必须能区分开，否则页面无法给出正确提示，也无法决定
// 该不该重试。
struct SyncState {
  SyncStatus status = SyncStatus::kIdle;
  std::string message;         // 面向日志/页面展示的简短说明
  uint64_t updated_at_ms = 0;  // 最近一次成功或失败的时刻
};

}  // namespace weather
