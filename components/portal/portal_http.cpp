#include "portal/portal_http.h"

#include <esp_err.h>
#include <esp_log.h>

#include <cstdlib>

#include "portal/portal_page.h"

namespace portal {
namespace {

constexpr char kTag[] = "portal";

// 接收请求体的等待上限（秒）。
constexpr int kRecvTimeoutSec = 5;

// 路由表容量。Routes() 有 9 条，加上兜底重定向。
constexpr int kMaxUriHandlers = 12;

// 表单体上限：按"4 个 64 字节字段 + 32 字节 adm + 百分号编码膨胀"留余量。
constexpr long kMaxFormBodyBytes = 2048;

// 取出注册路由时存入的 hooks。user_ctx 由 StartHttpServer 设置。
const PortalHttpHooks* HooksOf(httpd_req_t* req) {
  return static_cast<const PortalHttpHooks*>(req->user_ctx);
}

void SendPortalPage(httpd_req_t* req, const PortalHttpHooks& hooks,
                    const std::string& message) {
  const std::vector<std::string> empty;
  const std::vector<std::string>& options =
      hooks.wifi_options != nullptr ? *hooks.wifi_options : empty;
  const std::string page = BuildPortalPage(options, message);
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_send(req, page.c_str(), static_cast<ssize_t>(page.size()));
}

void HandlePortalPage(httpd_req_t* req, const PortalHttpHooks& hooks) {
  SendPortalPage(req, hooks, "");
}

void HandleRedirect(httpd_req_t* req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/");
  httpd_resp_send(req, nullptr, 0);
}

void HandleNoContent(httpd_req_t* req) {
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_send(req, nullptr, 0);
}

// 整块读出表单体。**只能调一次** —— httpd_req_recv() 会消费请求体，
// 之后再调只拿到 0 字节。所有字段都从这一个 string 里解析。
std::string ReadFormBody(httpd_req_t* req) {
  char length_header[16] = {};
  if (httpd_req_get_hdr_value_str(req, "Content-Length", length_header,
                                  sizeof(length_header)) != ESP_OK) {
    return {};
  }
  const long content_length = std::strtol(length_header, nullptr, 10);
  if (content_length <= 0 || content_length > kMaxFormBodyBytes) {
    return {};
  }
  std::string body(static_cast<std::size_t>(content_length), '\0');
  const int received = httpd_req_recv(req, body.data(), content_length);
  if (received <= 0) {
    return {};
  }
  body.resize(static_cast<std::size_t>(received));
  return body;
}

void HandleSave(httpd_req_t* req, const PortalHttpHooks& hooks) {
  const std::string body = ReadFormBody(req);
  PortalFormData form;
  form.ssid_selected = config::Trim(FormValue(body, "ssid_select"));
  form.ssid_manual = config::Trim(FormValue(body, "ssid_manual"));
  // 密码不 trim（首尾空格可能合法）
  form.password = FormValue(body, "password");
  form.adm = config::Trim(FormValue(body, "adm"));
  form.location = config::Trim(FormValue(body, "location"));

  const ValidationError error = Validate(form);
  if (error != ValidationError::kNone) {
    httpd_resp_set_status(req, "400 Bad Request");
    SendPortalPage(req, hooks, "Wi-Fi 名称、密码和位置都必须填写");
    ESP_LOGW(kTag, "表单校验失败: %s", ValidationErrorName(error));
    return;
  }

  config::DeviceConfig device_config;
  device_config.wifi_ssid = ResolveSsid(form);
  device_config.wifi_password = form.password;
  device_config.adm = form.adm;
  device_config.location = form.location;

  const bool saved = hooks.on_save != nullptr && *hooks.on_save &&
                     (*hooks.on_save)(device_config);
  if (!saved) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    SendPortalPage(req, hooks, "保存失败，请重试");
    ESP_LOGE(kTag, "配置保存失败");
    return;
  }

  // 先应答再重启，确保手机能收到确认页面
  SendPortalPage(req, hooks, "已保存。设备正在重启并连接你的 Wi-Fi。");
  ESP_LOGI(kTag, "配置已保存，准备重启");

  // 重启交给 net 任务：在 httpd 任务里直接 esp_restart() 会让这个响应
  // 来不及发出去。这里只置标志，由 CaptivePortal::Poll() 执行。
  if (hooks.restart_requested != nullptr) {
    *hooks.restart_requested = true;
  }
}

// 所有路由共用这一个入口：按请求路径分发。
esp_err_t HandleRequest(httpd_req_t* req) {
  const PortalHttpHooks* hooks = HooksOf(req);
  if (hooks == nullptr) {
    return ESP_FAIL;
  }

  // 请求行里的完整 URI（含查询串）—— 只看路径部分
  std::string path(req->uri);
  const std::size_t query = path.find('?');
  if (query != std::string::npos) {
    path.resize(query);
  }

  bool found = false;
  switch (ResolveRoute(path, found)) {
    case PortalAction::kPortalPage:
      HandlePortalPage(req, *hooks);
      break;
    case PortalAction::kSaveConfig:
      HandleSave(req, *hooks);
      break;
    case PortalAction::kNoContent:
      HandleNoContent(req);
      break;
    case PortalAction::kRedirectToRoot:
      HandleRedirect(req);
      break;
  }
  return ESP_OK;
}

// 注册一条 URI。user_ctx 指向 hooks，处理器据此找回门户状态。
esp_err_t RegisterUri(httpd_handle_t server, const char* path,
                      httpd_method_t method, const PortalHttpHooks* hooks) {
  httpd_uri_t uri = {};
  uri.uri = path;
  uri.method = method;
  uri.handler = &HandleRequest;
  uri.user_ctx = const_cast<PortalHttpHooks*>(hooks);
  return httpd_register_uri_handler(server, &uri);
}

}  // namespace

bool StartHttpServer(httpd_handle_t* server, const PortalHttpHooks* hooks) {
  if (server == nullptr || hooks == nullptr) {
    ESP_LOGE(kTag, "启动 HTTP 服务器的参数无效");
    return false;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = kMaxUriHandlers;
  config.recv_wait_timeout = kRecvTimeoutSec;
  config.lru_purge_enable = true;

  esp_err_t err = httpd_start(server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "启动 HTTP 服务器失败: %s", esp_err_to_name(err));
    *server = nullptr;
    return false;
  }

  // 路由表来自纯逻辑的 Routes()：注册与单测用的是同一份定义，
  // 不会出现"测到的路由和实际注册的不是一套"的漂移
  for (const PortalRoute& route : Routes()) {
    const httpd_method_t method =
        route.action == PortalAction::kSaveConfig ? HTTP_POST : HTTP_GET;
    err = RegisterUri(*server, route.path, method, hooks);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "注册路由 %s 失败: %s", route.path, esp_err_to_name(err));
      return false;
    }
  }

  // notFound -> 重定向到门户首页。
  // 强制门户靠这条兜底路由把任意探测请求拉回来，漏注册就会出现"连上了但不弹窗"
  err = RegisterUri(*server, "/*", HTTP_GET, hooks);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "注册兜底路由失败: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

void StopHttpServer(httpd_handle_t* server) {
  if (server == nullptr || *server == nullptr) {
    return;
  }
  httpd_stop(*server);
  *server = nullptr;
}

}  // namespace portal
