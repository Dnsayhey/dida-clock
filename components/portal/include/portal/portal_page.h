#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace portal {

// ---------------- 路由表 ----------------
//
// 路由表必须**完整**：少注册一条，就会有某个手机连上后不弹门户，
// 而这类问题极难复现。做成纯函数是为了能逐条验证。
enum class PortalAction {
  kPortalPage,      // 返回配网页
  kRedirectToRoot,  // 302 -> /
  kNoContent,       // 204
  kSaveConfig,      // 处理表单
};

struct PortalRoute {
  const char* path;
  PortalAction action;
};

// 全部显式注册的路由（不含 notFound）
const std::vector<PortalRoute>& Routes();

// 解析路由。未匹配任何显式路由时返回 kRedirectToRoot，并把 found 置为 false。
PortalAction ResolveRoute(const std::string& path, bool& found);

// ---------------- 页面与表单 ----------------

// HTML 转义。
//
// **必须做**：SSID 来自周边无线环境（或用户手输），是攻击者可控的字符串，
// 直接拼进 HTML 会形成注入。
std::string HtmlEscape(const std::string& value);

struct PortalFormData {
  std::string ssid_selected;  // 下拉框选中项
  std::string ssid_manual;    // 手动输入
  std::string password;
  std::string adm;       // 省市/上级区域
  std::string location;  // 区县
};

// 解析最终 SSID：下拉框优先，为空则用手动输入。
std::string ResolveSsid(const PortalFormData& form);

enum class ValidationError {
  kNone,
  kSsidMissing,
  kPasswordMissing,
  kLocationMissing,
};

const char* ValidationErrorName(ValidationError error);

// 校验表单。检查顺序固定（SSID -> 密码 -> 位置）：同一份表单永远报同一个
// 错误，用户改掉第一个问题后才看到第二个。
// 注意：**不做 trim** —— 调用方应先用 config::Trim() 规范化（本函数保持纯粹）。
// 从**已整块读出**的 urlencoded 表单体里取一个字段（含 %XX / '+' 解码）。
// 纯函数，可在宿主机上单测。
std::string FormValue(const std::string& body, const char* key);

ValidationError Validate(const PortalFormData& form);

// 生成配网页。
// wifi_options 是扫描到的 SSID 列表（调用方已去重、限长）。
// message 非空时在页面上显示提示（会被转义）。
std::string BuildPortalPage(const std::vector<std::string>& wifi_options,
                            const std::string& message);

// 入口 SSID：DIDA-XXXX，XXXX 为芯片 MAC 低 16 位的大写十六进制。
// 取 MAC 保证同固件的多台设备热点名不重复，且重启后保持不变。
std::string BuildPortalSsid(uint16_t chip_suffix);

// SoftAP 密码。固定值：配网页上直接展示给用户，不需要保密策略；
// 长度必须满足 WPA2-PSK 的 8~63 字节要求（见 portal_page.cpp）。
extern const char* const kPortalApPassword;

}  // namespace portal
