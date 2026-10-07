// storage 组件中纯逻辑部分的单测（主机运行）。
//
// 这些语义容易写错，且错了会直接表现为"配网后连不上"：
//   * 密码**不能** Trim（首尾空格可能是密码的一部分）
//   * 但用户误粘贴的换行/回车必须去掉
//   * 档位值要做区间夹取

#include "storage/device_config.h"
#include "test_framework.h"

using config::DeviceConfig;

namespace {

DeviceConfig MakeConfig() {
  DeviceConfig c;
  c.wifi_ssid = "MyWiFi";
  c.wifi_password = "secret123";
  c.adm = "浙江";
  c.location = "余杭";
  c.location_id = "101210101";
  c.location_lat = "30.42";
  c.location_lon = "120.30";
  return c;
}

}  // namespace

// ---------------- Trim ----------------

TEST_CASE(Trim_去掉首尾空白) {
  CHECK_EQ(config::Trim("  abc  "), std::string("abc"));
  CHECK_EQ(config::Trim("\t\nabc\r\n"), std::string("abc"));
}

TEST_CASE(Trim_全是空白得到空串) {
  CHECK_EQ(config::Trim("   \t\n "), std::string(""));
  CHECK_EQ(config::Trim(""), std::string(""));
}

TEST_CASE(Trim_不动中间的空格) {
  CHECK_EQ(config::Trim("  a b c  "), std::string("a b c"));
}

// ---------------- Normalize ----------------

TEST_CASE(Normalize_字符串字段做Trim) {
  DeviceConfig c = MakeConfig();
  c.wifi_ssid = "  MyWiFi \n";
  c.adm = " 浙江 ";
  c.location = "\t余杭 ";
  c.location_id = " 101210101 ";
  c.location_lat = " 30.42";
  c.location_lon = "120.30 ";

  const DeviceConfig n = config::Normalize(c);
  CHECK_EQ(n.wifi_ssid, std::string("MyWiFi"));
  CHECK_EQ(n.adm, std::string("浙江"));
  CHECK_EQ(n.location, std::string("余杭"));
  CHECK_EQ(n.location_id, std::string("101210101"));
  CHECK_EQ(n.location_lat, std::string("30.42"));
  CHECK_EQ(n.location_lon, std::string("120.30"));
}

// 这条是最容易写错、后果最直接的：Trim 密码会导致连不上。
TEST_CASE(Normalize_密码不做Trim) {
  DeviceConfig c = MakeConfig();
  c.wifi_password = "  space secret  ";

  const DeviceConfig n = config::Normalize(c);
  CHECK_EQ(n.wifi_password, std::string("  space secret  "));
}

// 但用户从网页表单误粘贴的换行必须去掉，否则 Wi-Fi 一定连不上。
TEST_CASE(Normalize_密码去掉换行与回车) {
  DeviceConfig c = MakeConfig();
  c.wifi_password = "secret\r\n";

  const DeviceConfig n = config::Normalize(c);
  CHECK_EQ(n.wifi_password, std::string("secret"));
}

TEST_CASE(Normalize_密码内部换行也去掉) {
  DeviceConfig c = MakeConfig();
  c.wifi_password = "sec\nret";

  const DeviceConfig n = config::Normalize(c);
  CHECK_EQ(n.wifi_password, std::string("secret"));
}

TEST_CASE(Normalize_档位越界被夹取) {
  DeviceConfig c = MakeConfig();
  c.backlight_mode = 99;
  c.theme_mode = -5;

  const DeviceConfig n = config::Normalize(c);
  CHECK_EQ(n.backlight_mode, 3);  // 亮度是 4 档
  CHECK_EQ(n.theme_mode, 0);      // 主题是 2 档，负值夹到深色
}

// 主题只有两档，上限必须是 1：曾经与亮度共用"上限 3"，于是 2/3 这种不存在的
// 主题值能存进 NVS（屏幕上按深色显示、设置页显示"未知"）。
TEST_CASE(Normalize_主题越界夹到两档以内) {
  DeviceConfig c = MakeConfig();
  c.theme_mode = 3;
  CHECK_EQ(config::Normalize(c).theme_mode, 1);

  c.theme_mode = 2;
  CHECK_EQ(config::Normalize(c).theme_mode, 1);

  c.theme_mode = 1;  // 合法值不动
  CHECK_EQ(config::Normalize(c).theme_mode, 1);
}

// 默认主题是**浅色**：这个值决定 NVS 里没有 theme 键时新设备开机显示什么。
TEST_CASE(DeviceConfig_默认主题是浅色) {
  const DeviceConfig fresh;
  CHECK_EQ(fresh.theme_mode, 1);
  CHECK_EQ(fresh.backlight_mode, 0);  // 亮度默认 AUTO，不受影响
}

TEST_CASE(Normalize_合法档位保持不变) {
  DeviceConfig c = MakeConfig();
  c.backlight_mode = 2;
  c.theme_mode = 1;

  const DeviceConfig n = config::Normalize(c);
  CHECK_EQ(n.backlight_mode, 2);
  CHECK_EQ(n.theme_mode, 1);
}

// ---------------- 判定函数 ----------------

TEST_CASE(IsWifiConfigured_两者都非空才算已配置) {
  DeviceConfig c = MakeConfig();
  CHECK(config::IsWifiConfigured(c));

  c.wifi_ssid.clear();
  CHECK(!config::IsWifiConfigured(c));

  c = MakeConfig();
  c.wifi_password.clear();
  CHECK(!config::IsWifiConfigured(c));
}

// 空密码的开放网络同样算"未配置"：设备不会去连它
TEST_CASE(IsWifiConfigured_空密码视为未配置) {
  DeviceConfig c = MakeConfig();
  c.wifi_password = "";
  CHECK(!config::IsWifiConfigured(c));
}

TEST_CASE(IsLocationConfigured_看位置名) {
  DeviceConfig c = MakeConfig();
  CHECK(config::IsLocationConfigured(c));

  c.location.clear();
  CHECK(!config::IsLocationConfigured(c));
}

TEST_CASE(IsWeatherQueryReady_需要id与经纬度) {
  DeviceConfig c = MakeConfig();
  CHECK(config::IsWeatherQueryReady(c));

  // 只有位置名、没有 id 和坐标时还不具备查询条件
  DeviceConfig partial;
  partial.location = "余杭";
  CHECK(!config::IsWeatherQueryReady(partial));

  // 缺经纬度则空气质量查不了
  DeviceConfig no_coords = MakeConfig();
  no_coords.location_lat.clear();
  CHECK(!config::IsWeatherQueryReady(no_coords));
}

// ---------------- NVS 键名稳定性 ----------------
//
// 这些字面量是**既有设备上已经落盘的格式**，改动会丢用户配置、
// 让 OTA 升级后的设备读到空配置。用测试把它们钉住。
TEST_CASE(Nvs命名空间与键名是既有落盘格式) {
  CHECK_EQ(std::string(config::kNvsNamespace), std::string("dida"));
  CHECK_EQ(std::string(config::kKeyWifiSsid), std::string("ssid"));
  CHECK_EQ(std::string(config::kKeyWifiPassword), std::string("pwd"));
  CHECK_EQ(std::string(config::kKeyAdm), std::string("adm"));
  CHECK_EQ(std::string(config::kKeyLocation), std::string("loc"));
  CHECK_EQ(std::string(config::kKeyLocationId), std::string("loc_id"));
  CHECK_EQ(std::string(config::kKeyLocationLat), std::string("loc_lat"));
  CHECK_EQ(std::string(config::kKeyLocationLon), std::string("loc_lon"));
  CHECK_EQ(std::string(config::kKeyBacklightMode), std::string("blm"));
  CHECK_EQ(std::string(config::kKeyThemeMode), std::string("theme"));
}
