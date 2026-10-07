#include "portal/portal_page.h"

#include <cstdio>

namespace portal {

// WPA2-PSK 要求 8~63 字节，这是满足要求的最短值（便于用户在手机上输入）。
const char* const kPortalApPassword = "12345678";

namespace {

constexpr char kPageHead[] =
    "<!doctype html><html lang=\"zh-CN\"><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>DIDA 配网</title>"
    "<style>"
    // 与设备主题一致的黑白配色，不带任何彩色元素。
    // :root 里的两行是专门用来压掉浏览器默认蓝色的：
    //   color-scheme      —— 防止系统深色模式改写表单控件配色
    //   accent-color      —— 单选/多选/进度条等的强调色默认是蓝
    // 再配合下面的 appearance:none 与显式 :focus，表单不会出现蓝色边框。
    ":root{color-scheme:light;accent-color:#111;}"
    "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe "
    "UI',sans-serif;background:#fff;color:#111;}"
    ".wrap{max-width:420px;margin:0 auto;padding:28px 20px;}"
    ".card{border:1px solid #e2e2e2;border-radius:18px;padding:22px;"
    "box-shadow:0 12px 40px rgba(0,0,0,.08);}"
    "h1{font-size:26px;margin:0 0 8px;color:#111;}"
    "p{line-height:1.6;color:#555;}"
    "label{display:block;margin-top:16px;font-weight:700;color:#111;}"
    "input,select{width:100%;box-sizing:border-box;margin-top:8px;border:1px "
    "solid #ccc;border-radius:12px;padding:13px;font-size:16px;"
    "background:#fff;color:#111;appearance:none;-webkit-appearance:none;}"
    "input:focus,select:focus{outline:2px solid #111;outline-offset:1px;"
    "border-color:#111;}"
    "button{width:100%;margin-top:22px;border:0;border-radius:14px;"
    "padding:14px;font-size:17px;font-weight:800;background:#111;"
    "color:#fff;}"
    ".msg{margin:14px 0;padding:10px 12px;border-radius:12px;"
    "background:#f2f2f2;color:#111;}"
    ".hint{font-size:13px;color:#888;margin:8px 0 0;}"
    "</style></head><body><main class=\"wrap\"><section class=\"card\">"
    "<h1>DIDA 配网</h1>"
    "<p>选择家中的 Wi-Fi 并填写天气位置。"
    "保存后设备会自动重启并连接。</p>";

constexpr char kPageFormStart[] =
    "<form method=\"post\" action=\"/save\">"
    "<label>Wi-Fi 名称</label>"
    "<select name=\"ssid_select\" id=\"ssidSelect\">"
    "<option value=\"\">手动输入 / 列表里没有我的网络</option>";

constexpr char kPageFormMid[] =
    "</select>"
    "<input name=\"ssid_manual\" id=\"ssidManual\" "
    "placeholder=\"Wi-Fi 名称\" maxlength=\"64\">"
    "<p class=\"hint\">列表里没有你的网络时，请在上面手动输入。</p>"
    "<label>Wi-Fi 密码</label>"
    "<input name=\"password\" type=\"password\" required maxlength=\"64\">"
    "<label>省份 / 地区</label>"
    "<input name=\"adm\" placeholder=\"例如 浙江\" maxlength=\"32\">"
    "<label>城市 / 位置</label>"
    "<input name=\"location\" required placeholder=\"例如 杭州\" "
    "maxlength=\"32\">"
    "<button id=\"submitButton\" type=\"submit\">保存并连接</button>"
    "</form><script>"
    "const s=document.getElementById('ssidSelect'),"
    "m=document.getElementById('ssidManual');"
    "function sync(){m.required=!s.value;"
    "m.style.display=s.value?'none':'block';}"
    "s.addEventListener('change',sync);sync();"
    "document.querySelector('form').addEventListener('submit',()=>{"
    "const b=document.getElementById('submitButton');"
    "b.disabled=true;b.textContent='保存中…';});"
    "</script></section></main></body></html>";

}  // namespace

const std::vector<PortalRoute>& Routes() {
  // 顺序无关（httpd 按精确路径匹配），但集中列在这里便于与测试逐条比对。
  static const std::vector<PortalRoute> kRoutes = {
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
  return kRoutes;
}

PortalAction ResolveRoute(const std::string& path, bool& found) {
  for (const PortalRoute& route : Routes()) {
    if (path == route.path) {
      found = true;
      return route.action;
    }
  }
  // 兜底：未匹配任何显式路由时也重定向到门户首页 —— 强制门户要求
  // 任意探测 URL 都被拉回配网页，否则手机上不会弹出登录页。
  found = false;
  return PortalAction::kRedirectToRoot;
}

std::string HtmlEscape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char c : value) {
    switch (c) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      default:
        escaped += c;
        break;
    }
  }
  return escaped;
}

std::string ResolveSsid(const PortalFormData& form) {
  if (!form.ssid_selected.empty()) {
    return form.ssid_selected;
  }
  return form.ssid_manual;
}

const char* ValidationErrorName(ValidationError error) {
  switch (error) {
    case ValidationError::kNone:
      return "none";
    case ValidationError::kSsidMissing:
      return "ssid_missing";
    case ValidationError::kPasswordMissing:
      return "password_missing";
    case ValidationError::kLocationMissing:
      return "location_missing";
  }
  return "unknown";
}

std::string FormValue(const std::string& body, const char* key) {
  // 从 urlencoded 表单体里取一个字段，并做 %XX / '+' 解码。
  //
  // 注意：调用方必须**先整块读出请求体再调本函数**。httpd_req_recv() 会消费
  // 请求体，每个字段各读一次的话只有第一个能拿到值 —— 这正是实机上
  // "填完表单却提示字段不能为空"的根因。
  const std::string pattern = std::string(key) + "=";
  std::size_t pos = 0;
  while ((pos = body.find(pattern, pos)) != std::string::npos) {
    // 必须是字段边界（开头或 & 之后），否则 "adm=" 会误命中 "loc_adm="
    if (pos == 0 || body[pos - 1] == '&') {
      const std::size_t value_start = pos + pattern.size();
      std::size_t value_end = body.find('&', value_start);
      if (value_end == std::string::npos) {
        value_end = body.size();
      }
      const std::string value =
          body.substr(value_start, value_end - value_start);
      std::string decoded;
      decoded.reserve(value.size());
      for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
          decoded += ' ';
        } else if (value[i] == '%' && i + 2 < value.size()) {
          const auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
          };
          decoded +=
              static_cast<char>(hex(value[i + 1]) * 16 + hex(value[i + 2]));
          i += 2;
        } else {
          decoded += value[i];
        }
      }
      return decoded;
    }
    pos += pattern.size();
  }
  return {};
}

ValidationError Validate(const PortalFormData& form) {
  if (ResolveSsid(form).empty()) {
    return ValidationError::kSsidMissing;
  }
  if (form.password.empty()) {
    return ValidationError::kPasswordMissing;
  }
  if (form.location.empty()) {
    return ValidationError::kLocationMissing;
  }
  return ValidationError::kNone;
}

std::string BuildPortalPage(const std::vector<std::string>& wifi_options,
                            const std::string& message) {
  std::string page;
  page.reserve(4200);
  page += kPageHead;

  if (!message.empty()) {
    page += "<div class=\"msg\">";
    page += HtmlEscape(message);
    page += "</div>";
  }

  page += kPageFormStart;
  for (const std::string& ssid : wifi_options) {
    page += "<option value=\"";
    page += HtmlEscape(ssid);
    page += "\">";
    page += HtmlEscape(ssid);
    page += "</option>";
  }
  page += kPageFormMid;
  return page;
}

std::string BuildPortalSsid(uint16_t chip_suffix) {
  // "DIDA-" 5 字符 + 4 位十六进制 + NUL = 10 字节，缓冲区必须放得下：
  // snprintf 会**静默截断**，截断后 AP 名字缺字符，而 AP 名字正是用户
  // 在手机列表里识别这台设备的方式。
  char suffix[16];
  std::snprintf(suffix, sizeof(suffix), "DIDA-%04X",
                static_cast<unsigned>(chip_suffix));
  return suffix;
}

}  // namespace portal
