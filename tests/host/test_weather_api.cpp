// weather 组件的 URL 构造与编码单测（主机运行）。

#include "test_framework.h"
#include "weather/weather_api.h"

using weather::UrlEncode;

TEST_CASE(UrlEncode_空格转为加号) {
  CHECK_EQ(UrlEncode("hello world"), std::string("hello+world"));
}

TEST_CASE(UrlEncode_保留未保留字符) {
  CHECK_EQ(UrlEncode("abcXYZ019"), std::string("abcXYZ019"));
  CHECK_EQ(UrlEncode("-_.~"), std::string("-_.~"));
}

TEST_CASE(UrlEncode_特殊字符转百分号大写) {
  CHECK_EQ(UrlEncode("a/b"), std::string("a%2Fb"));
  CHECK_EQ(UrlEncode("a:b"), std::string("a%3Ab"));
  CHECK_EQ(UrlEncode("k=v&x"), std::string("k%3Dv%26x"));
  CHECK_EQ(UrlEncode("1+1"), std::string("1%2B1"));
}

TEST_CASE(UrlEncode_中文UTF8逐字节编码) {
  // "余杭" 的 UTF-8 字节序列为 E4 BD 99 E6 9D AD
  CHECK_EQ(UrlEncode("余杭"), std::string("%E4%BD%99%E6%9D%AD"));
}

TEST_CASE(UrlEncode_大写十六进制) {
  // 0x2f 的低半字节是 f，必须是 %2F 而不是 %2f
  const std::string encoded = UrlEncode("/");
  CHECK_EQ(encoded, std::string("%2F"));
  CHECK(encoded.find("%2f") == std::string::npos);
}

TEST_CASE(UrlEncode_空串) { CHECK_EQ(UrlEncode(""), std::string("")); }

// ---------------- 各端点 URL ----------------

TEST_CASE(Url_城市查询_无adm) {
  const std::string url =
      weather::BuildCityLookupUrl("https://api.example.com", "KEY123", "余杭");
  CHECK_EQ(url, std::string("https://api.example.com/geo/v2/city/lookup"
                            "?key=KEY123&location=%E4%BD%99%E6%9D%AD&lang=zh"));
}

TEST_CASE(Url_城市查询_带adm) {
  const std::string url = weather::BuildCityLookupUrl("https://api.example.com",
                                                      "KEY123", "余杭", "杭州");
  CHECK_EQ(url, std::string("https://api.example.com/geo/v2/city/lookup"
                            "?key=KEY123&location=%E4%BD%99%E6%9D%AD"
                            "&adm=%E6%9D%AD%E5%B7%9E&lang=zh"));
}

TEST_CASE(Url_城市查询_空adm不加参数) {
  const std::string url =
      weather::BuildCityLookupUrl("https://api.example.com", "K", "余杭", "");
  CHECK(url.find("&adm=") == std::string::npos);
}

TEST_CASE(Url_实时天气) {
  const std::string url = weather::BuildWeatherNowUrl("https://api.example.com",
                                                      "KEY", "101210101");
  CHECK_EQ(url, std::string("https://api.example.com/v7/weather/now"
                            "?key=KEY&location=101210101&lang=zh"));
}

TEST_CASE(Url_七日预报) {
  const std::string url = weather::BuildDailyForecastUrl(
      "https://api.example.com", "KEY", "101210101");
  CHECK_EQ(url, std::string("https://api.example.com/v7/weather/7d"
                            "?key=KEY&location=101210101&lang=zh"));
}

TEST_CASE(Url_空气质量_经纬度进路径) {
  const std::string url = weather::BuildAirQualityUrl("https://api.example.com",
                                                      "KEY", "30.42", "120.30");
  CHECK_EQ(url, std::string("https://api.example.com/airquality/v1/current/"
                            "30.42/120.30?key=KEY&lang=zh"));
}

TEST_CASE(Url_空气质量_负经度也被编码正确) {
  const std::string url = weather::BuildAirQualityUrl(
      "https://api.example.com", "KEY", "-33.87", "151.21");
  CHECK_EQ(url, std::string("https://api.example.com/airquality/v1/current/"
                            "-33.87/151.21?key=KEY&lang=zh"));
}

// 界面是中文，所以所有请求都要服务端返回中文文本。
// 少了这个参数会拿到英文，本地还得再翻译一层。
TEST_CASE(Url_所有端点都要求中文) {
  const std::string city = weather::BuildCityLookupUrl(
      "https://api.example.com", "K", "余杭", "杭州");
  const std::string now =
      weather::BuildWeatherNowUrl("https://api.example.com", "K", "101210101");
  const std::string daily = weather::BuildDailyForecastUrl(
      "https://api.example.com", "K", "101210101");
  const std::string air = weather::BuildAirQualityUrl("https://api.example.com",
                                                      "K", "30.42", "120.30");
  CHECK(city.find("lang=zh") != std::string::npos);
  CHECK(now.find("lang=zh") != std::string::npos);
  CHECK(daily.find("lang=zh") != std::string::npos);
  CHECK(air.find("lang=zh") != std::string::npos);
}
