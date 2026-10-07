// 强制门户纯逻辑的单测（主机运行）。
//
// 三块内容刻意做成纯函数，因为它们在实机上极难验证：
//   * 路由表 —— 少一条会导致"某个手机连上后不弹门户"，且只在特定机型复现
//   * HTML 转义 —— SSID 是攻击者可控字符串，注入问题在实机上不易察觉
//   * DNS 报文 —— 逐字节格式，错了就是"DNS 没响应"，看不出原因

#include <cstdint>
#include <string>
#include <vector>

#include "portal/dns_message.h"
#include "portal/portal_page.h"
#include "test_framework.h"

using portal::BuildDnsResponse;
using portal::BuildPortalPage;
using portal::BuildPortalSsid;
using portal::FormValue;
using portal::HtmlEscape;
using portal::PortalAction;
using portal::PortalFormData;
using portal::ResolveRoute;
using portal::ResolveSsid;
using portal::Routes;
using portal::Validate;
using portal::ValidationError;

// ---------------- 路由表 ----------------

// 门户共 9 条显式路由，逐条比对：少一条就会有手机连上后不弹门户
TEST_CASE(Portal_路由表逐条覆盖) {
  const auto& routes = Routes();
  CHECK_EQ(routes.size(), 9u);

  // 路径 -> 期望动作
  struct Expect {
    const char* path;
    PortalAction action;
  };
  const Expect expected[] = {
      {"/", PortalAction::kPortalPage},
      {"/generate_204", PortalAction::kRedirectToRoot},
      {"/gen_204", PortalAction::kRedirectToRoot},
      {"/hotspot-detect.html", PortalAction::kRedirectToRoot},
      {"/library/test/success.html", PortalAction::kRedirectToRoot},
      {"/connecttest.txt", PortalAction::kRedirectToRoot},
      {"/ncsi.txt", PortalAction::kRedirectToRoot},
      {"/favicon.ico", PortalAction::kNoContent},
      {"/save", PortalAction::kSaveConfig},
  };

  for (const Expect& e : expected) {
    bool found = false;
    const PortalAction action = ResolveRoute(e.path, found);
    CHECK(found);
    CHECK_EQ(static_cast<int>(action), static_cast<int>(e.action));
  }
}

// 每个厂商的探测地址都必须被覆盖，否则那家的手机不会自动弹门户
TEST_CASE(Portal_覆盖各厂商的联网探测地址) {
  // Android / Chrome
  for (const char* path : {"/generate_204", "/gen_204"}) {
    bool found = false;
    ResolveRoute(path, found);
    CHECK(found);
  }
  // Apple iOS / macOS
  for (const char* path :
       {"/hotspot-detect.html", "/library/test/success.html"}) {
    bool found = false;
    ResolveRoute(path, found);
    CHECK(found);
  }
  // Windows
  for (const char* path : {"/connecttest.txt", "/ncsi.txt"}) {
    bool found = false;
    ResolveRoute(path, found);
    CHECK(found);
  }
}

TEST_CASE(Portal_未注册路径走兜底重定向) {
  bool found = true;
  const PortalAction action = ResolveRoute("/some/random/path", found);
  CHECK(!found);  // 未匹配任何显式路由：found 必须为 false
  CHECK(action == PortalAction::kRedirectToRoot);
}

TEST_CASE(Portal_根路径返回配网页) {
  bool found = false;
  CHECK(ResolveRoute("/", found) == PortalAction::kPortalPage);
  CHECK(found);
}

// ---------------- HTML 转义 ----------------

TEST_CASE(Portal_HTML转义覆盖危险字符) {
  CHECK_EQ(HtmlEscape("<script>"), std::string("&lt;script&gt;"));
  CHECK_EQ(HtmlEscape("a&b"), std::string("a&amp;b"));
  CHECK_EQ(HtmlEscape("say \"hi\""), std::string("say &quot;hi&quot;"));
}

TEST_CASE(Portal_HTML转义保留普通字符与中文) {
  CHECK_EQ(HtmlEscape("我的WiFi-5G"), std::string("我的WiFi-5G"));
}

// 关键安全回归：SSID 可以被周边 AP 控制，必须转义后才能进 HTML
TEST_CASE(Portal_中文与引号不影响转义) {
  CHECK_EQ(HtmlEscape("\"><img src=x>"),
           std::string("&quot;&gt;&lt;img src=x&gt;"));
}

// ---------------- 表单解析与校验 ----------------

TEST_CASE(Portal_下拉框优先于手动输入) {
  PortalFormData form;
  form.ssid_selected = "HomeWiFi";
  form.ssid_manual = "OtherWiFi";
  CHECK_EQ(ResolveSsid(form), std::string("HomeWiFi"));
}

TEST_CASE(Portal_下拉框为空时用手动输入) {
  PortalFormData form;
  form.ssid_selected = "";
  form.ssid_manual = "MyWiFi";
  CHECK_EQ(ResolveSsid(form), std::string("MyWiFi"));
}

TEST_CASE(Portal_校验按字段顺序) {
  PortalFormData form;
  // 全空 -> 先报 SSID
  CHECK(Validate(form) == ValidationError::kSsidMissing);

  // SSID 有、密码空 -> 报密码
  form.ssid_manual = "MyWiFi";
  CHECK(Validate(form) == ValidationError::kPasswordMissing);

  // SSID + 密码有、位置空 -> 报位置
  form.password = "secret123";
  CHECK(Validate(form) == ValidationError::kLocationMissing);

  // 齐全 -> 通过
  form.location = "余杭";
  CHECK(Validate(form) == ValidationError::kNone);
}

TEST_CASE(Portal_校验不要求adm字段) {
  PortalFormData form;
  form.ssid_selected = "MyWiFi";
  form.password = "secret123";
  form.location = "余杭";
  form.adm = "";  // 省市不是必填项
  CHECK(Validate(form) == ValidationError::kNone);
}

// ---------------- 配网页 ----------------

TEST_CASE(Portal_配网页包含全部表单字段) {
  const std::string page = BuildPortalPage({"HomeWiFi"}, "");
  for (const char* needle :
       {"<!doctype html>", "charset=\"utf-8\"", "action=\"/save\"",
        "name=\"ssid_select\"", "name=\"ssid_manual\"", "name=\"password\"",
        "name=\"adm\"", "name=\"location\"", "type=\"password\"",
        "method=\"post\""}) {
    CHECK(page.find(needle) != std::string::npos);
  }
}

TEST_CASE(Portal_配网页列出扫描到的网络) {
  const std::string page = BuildPortalPage({"A", "B"}, "");
  CHECK(page.find(">A</option>") != std::string::npos);
  CHECK(page.find(">B</option>") != std::string::npos);
}

TEST_CASE(Portal_配网页在无网络时仍可用) {
  const std::string page = BuildPortalPage({}, "");
  CHECK(page.find("name=\"ssid_manual\"") != std::string::npos);
}

// 安全回归：恶意 SSID 不能在页面里形成可执行标签
TEST_CASE(Portal_配网页转义恶意SSID) {
  const std::string page = BuildPortalPage({"<script>alert(1)</script>"}, "");
  CHECK(page.find("<script>alert(1)</script>") == std::string::npos);
  CHECK(page.find("&lt;script&gt;alert(1)&lt;/script&gt;") !=
        std::string::npos);
}

TEST_CASE(Portal_配网页转义提示信息) {
  const std::string page = BuildPortalPage({}, "<b>错误</b>");
  CHECK(page.find("<b>错误</b>") == std::string::npos);
  CHECK(page.find("&lt;b&gt;") != std::string::npos);
}

TEST_CASE(Portal_配网页无提示时不渲染提示框) {
  CHECK(BuildPortalPage({}, "").find("class=\"msg\"") == std::string::npos);
  CHECK(BuildPortalPage({}, "有错").find("class=\"msg\"") != std::string::npos);
}

// ---------------- 入口 SSID ----------------

TEST_CASE(Portal_入口SSID格式) {
  CHECK_EQ(BuildPortalSsid(0x1A2B), std::string("DIDA-1A2B"));
  CHECK_EQ(BuildPortalSsid(0x00FF), std::string("DIDA-00FF"));
  CHECK_EQ(BuildPortalSsid(0x0000), std::string("DIDA-0000"));
}

TEST_CASE(Portal_AP密码满足WPA2要求) {
  CHECK_EQ(std::string(portal::kPortalApPassword), std::string("12345678"));
}

// ---------------- DNS 报文 ----------------

namespace {

// 构造一个针对 name 的 A 记录查询
std::vector<uint8_t> MakeQuery(const std::string& name, uint16_t id = 0x1234,
                               uint16_t qdcount = 1) {
  std::vector<uint8_t> q;
  q.push_back(static_cast<uint8_t>(id >> 8));
  q.push_back(static_cast<uint8_t>(id & 0xFF));
  q.push_back(0x01);  // flags: RD
  q.push_back(0x00);
  q.push_back(static_cast<uint8_t>(qdcount >> 8));
  q.push_back(static_cast<uint8_t>(qdcount & 0xFF));
  for (int i = 0; i < 6; ++i) {
    q.push_back(0x00);  // AN/NS/AR count
  }
  // QNAME
  std::size_t start = 0;
  while (start < name.size()) {
    const std::size_t dot = name.find('.', start);
    const std::size_t end = dot == std::string::npos ? name.size() : dot;
    const std::size_t len = end - start;
    q.push_back(static_cast<uint8_t>(len));
    for (std::size_t i = start; i < end; ++i) {
      q.push_back(static_cast<uint8_t>(name[i]));
    }
    if (dot == std::string::npos) {
      break;
    }
    start = dot + 1;
  }
  q.push_back(0x00);  // 名字结束
  q.push_back(0x00);  // QTYPE = A
  q.push_back(0x01);
  q.push_back(0x00);  // QCLASS = IN
  q.push_back(0x01);
  return q;
}

}  // namespace

TEST_CASE(Dns_应答报文逐字节正确) {
  const std::vector<uint8_t> query = MakeQuery("example.com");
  uint8_t out[512] = {};
  std::size_t out_len = 0;

  // 192.168.4.1 的网络字节序
  const uint32_t ip_be = 0x0104A8C0;
  CHECK(BuildDnsResponse(query.data(), query.size(), ip_be, out, sizeof(out),
                         out_len));

  // 头部
  CHECK_EQ(out[0], 0x12);  // ID 原样回填
  CHECK_EQ(out[1], 0x34);
  CHECK_EQ(out[2], 0x81);  // QR=1 AA=1 RD=1
  CHECK_EQ(out[3], 0x80);  // RA=1 RCODE=0
  CHECK_EQ(out[4], 0x00);  // QDCOUNT = 1
  CHECK_EQ(out[5], 0x01);
  CHECK_EQ(out[6], 0x00);  // ANCOUNT = 1
  CHECK_EQ(out[7], 0x01);
  CHECK_EQ(out[8], 0x00);  // NSCOUNT = 0
  CHECK_EQ(out[9], 0x00);
  CHECK_EQ(out[10], 0x00);  // ARCOUNT = 0
  CHECK_EQ(out[11], 0x00);

  // 问题段原样回显
  CHECK_EQ(out[12], 7);
  CHECK_EQ(std::string(reinterpret_cast<char*>(out + 13), 7),
           std::string("example"));

  // 应答段
  const std::size_t answer = 12 + 13 + 4;
  CHECK_EQ(out[answer + 0], 0xC0);  // 压缩指针 -> 偏移 12
  CHECK_EQ(out[answer + 1], 0x0C);
  CHECK_EQ(out[answer + 2], 0x00);  // TYPE = A
  CHECK_EQ(out[answer + 3], 0x01);
  CHECK_EQ(out[answer + 4], 0x00);  // CLASS = IN
  CHECK_EQ(out[answer + 5], 0x01);
  // 应答段布局：NAME(2) TYPE(2) CLASS(2) TTL(4) RDLENGTH(2) RDATA(4)
  CHECK_EQ(out[answer + 6], 0x00);  // TTL = 60
  CHECK_EQ(out[answer + 7], 0x00);
  CHECK_EQ(out[answer + 8], 0x00);
  CHECK_EQ(out[answer + 9], 0x3C);
  CHECK_EQ(out[answer + 10], 0x00);  // RDLENGTH = 4
  CHECK_EQ(out[answer + 11], 0x04);
  CHECK_EQ(out[answer + 12], 0xC0);  // 192.168.4.1 网络字节序
  CHECK_EQ(out[answer + 13], 0xA8);  // 168
  CHECK_EQ(out[answer + 14], 0x04);  // 4
  CHECK_EQ(out[answer + 15], 0x01);  // 1

  // 总长 = 问题段结束 + 16
  CHECK_EQ(out_len, answer + 16);
}

// 关键：不能应答"应答报文"，否则两个 DNS 会互相打乒乓
TEST_CASE(Dns_不应答已是响应的报文) {
  std::vector<uint8_t> query = MakeQuery("example.com");
  query[2] |= 0x80;  // 置 QR 位

  uint8_t out[512] = {};
  std::size_t out_len = 0;
  CHECK(!BuildDnsResponse(query.data(), query.size(), 0x0104A8C0, out,
                          sizeof(out), out_len));
}

TEST_CASE(Dns_拒绝问题段数量不为1的报文) {
  const std::vector<uint8_t> query = MakeQuery("example.com", 0x1234, 2);
  uint8_t out[512] = {};
  std::size_t out_len = 0;
  CHECK(!BuildDnsResponse(query.data(), query.size(), 0x0104A8C0, out,
                          sizeof(out), out_len));
}

TEST_CASE(Dns_拒绝过短的报文) {
  const uint8_t tiny[4] = {0x12, 0x34, 0x01, 0x00};
  uint8_t out[512] = {};
  std::size_t out_len = 0;
  CHECK(!BuildDnsResponse(tiny, sizeof(tiny), 0x0104A8C0, out, sizeof(out),
                          out_len));
}

TEST_CASE(Dns_问题段被截断时拒绝) {
  std::vector<uint8_t> query = MakeQuery("example.com");
  query.resize(query.size() - 2);  // 切掉 QCLASS
  uint8_t out[512] = {};
  std::size_t out_len = 0;
  CHECK(!BuildDnsResponse(query.data(), query.size(), 0x0104A8C0, out,
                          sizeof(out), out_len));
}

TEST_CASE(Dns_输出缓冲区不足时拒绝) {
  const std::vector<uint8_t> query = MakeQuery("example.com");
  uint8_t out[16] = {};
  std::size_t out_len = 0;
  CHECK(!BuildDnsResponse(query.data(), query.size(), 0x0104A8C0, out,
                          sizeof(out), out_len));
}

// 不同长度的域名都要能正确解析问题段
TEST_CASE(Dns_不同长度域名都能解析) {
  for (const char* name : {"a.io", "example.com", "www.google.com",
                           "connectivity-check.android.com"}) {
    const std::vector<uint8_t> query = MakeQuery(name);
    uint8_t out[512] = {};
    std::size_t out_len = 0;
    CHECK(BuildDnsResponse(query.data(), query.size(), 0x0104A8C0, out,
                           sizeof(out), out_len));
    // 问题段结束位置 + 16 必须等于总长
    CHECK_EQ(out_len, query.size() + 16);
  }
}

// ---------------- 表单体解析 ----------------

// 一次请求体必须能解析出**全部**字段。
//
// 边界在于：httpd_req_recv() 只能读一次请求体。若按字段逐个去读，
// 只有第一个字段有值、其余全空。本用例守住这条边界。
TEST_CASE(FormValue_一次请求体能解析出全部字段) {
  // 真实提交会长的样子：ssid_select 空、ssid_manual 填了、地名是中文
  const std::string body =
      "ssid_select=&ssid_manual=702&password=abc123&adm=%E6%B5%99%E6%B1%9F"
      "&location=%E6%9D%AD%E5%B7%9E";

  CHECK_EQ(FormValue(body, "ssid_select"), std::string(""));
  CHECK_EQ(FormValue(body, "ssid_manual"), std::string("702"));
  CHECK_EQ(FormValue(body, "password"), std::string("abc123"));
  CHECK_EQ(FormValue(body, "location"), std::string("杭州"));
  CHECK_EQ(FormValue(body, "adm"), std::string("浙江"));
}

TEST_CASE(FormValue_从下拉选择时manual为空) {
  const std::string body =
      "ssid_select=702&ssid_manual=&password=pw&adm=&location=Hangzhou";
  CHECK_EQ(FormValue(body, "ssid_select"), std::string("702"));
  CHECK_EQ(FormValue(body, "ssid_manual"), std::string(""));
  // 靠后的字段同样要能取到
  CHECK_EQ(FormValue(body, "password"), std::string("pw"));
  CHECK_EQ(FormValue(body, "location"), std::string("Hangzhou"));
}

TEST_CASE(FormValue_加号解码为空格) {
  CHECK_EQ(FormValue("ssid=My+Wi-Fi", "ssid"), std::string("My Wi-Fi"));
}

TEST_CASE(FormValue_字段名必须完整匹配) {
  // "adm=" 不能误命中 "loc_adm="，反之亦然
  const std::string body = "loc_adm=WRONG&adm=Zhejiang";
  CHECK_EQ(FormValue(body, "adm"), std::string("Zhejiang"));
}

TEST_CASE(FormValue_缺失字段返回空) {
  CHECK_EQ(FormValue("ssid=702", "password"), std::string(""));
  CHECK_EQ(FormValue("", "ssid"), std::string(""));
}

TEST_CASE(FormValue_百分号编码不完整时不越界) {
  // 结尾是孤立的 '%'，不能读越界
  CHECK_EQ(FormValue("ssid=ab%", "ssid"), std::string("ab%"));
  CHECK_EQ(FormValue("ssid=ab%4", "ssid"), std::string("ab%4"));
}
