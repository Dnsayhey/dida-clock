#include "startup_task.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <iterator>

#include "app/runtime_status.h"
#include "backlight/backlight_policy.h"
#include "net/network_status.h"
#include "net/sntp.h"
#include "net/wifi_station.h"
#include "pipeline/weather_sync.h"
#include "runtime.h"
#include "scheduler/app_scheduler.h"
#include "sdkconfig.h"
#include "time_config.h"
#include "weather_config.h"

// 候选列表必须放得进 lwIP 的服务器槽位。越界的索引在 sntp_setservername() 里
// 被**静默丢弃**，表现为"配了备用服务器却永远不会切过去" —— 一个只会在
// 主服务器恰好挂掉时才暴露的问题。所以把这条约束放在编译期。
static_assert(std::size(dida::kNtpServers) <=
                  static_cast<std::size_t>(CONFIG_LWIP_SNTP_MAX_SERVERS),
              "NTP 候选列表比 CONFIG_LWIP_SNTP_MAX_SERVERS 多："
              "请同时提高 sdkconfig 里的这个值");

namespace dida {
namespace {

constexpr char kTag[] = "dida";

// 任务栈与优先级。
//
// 栈给到 12 KB：这一侧要跑 HTTPS 请求，mbedTLS 握手吃栈较深。
constexpr uint32_t kStartupTaskStack = 12288;
constexpr UBaseType_t kStartupTaskPriority = 4;

// 周期同步间隔
constexpr uint32_t kWeatherSyncIntervalMs = 30UL * 60 * 1000;     // 30 分钟
constexpr uint32_t kWeather7dIntervalMs = 6UL * 60 * 60 * 1000;   // 6 小时
constexpr uint32_t kAirQualitySyncIntervalMs = 60UL * 60 * 1000;  // 60 分钟
constexpr uint32_t kNetworkWatchIntervalMs = 10UL * 1000;         // 10 秒

// 调度轮询间隔：决定"到期判定的时间分辨率"，不是任务周期
// （RunDue 每轮对每个任务至多执行一次）
constexpr uint32_t kSchedulerTickMs = 1000;

// 背光自适应周期：3 秒。环境光变化是慢变量，采样再快也只是反复改 PWM，
// 没有可见收益，反而多占 net 任务的时间。
constexpr uint32_t kBacklightIntervalMs = 3000;

// 通配 DNS 轮询周期。套接字是非阻塞的，空闲时立即返回。
constexpr uint32_t kDnsPollIntervalMs = 100;

// 导航回调。在 xTaskCreate **之前**写入，任务启动后只读 ——
// 单核、且写入发生于任务创建之前，因此不需要额外同步。
NavigationRequest g_on_navigate = nullptr;

// 跨越"启动序列"与"调度循环"两个阶段的状态。
//
// 为什么这些对象必须一起存活：调度器注册的周期任务回调按引用捕获 sync / wifi，
// 而调度循环在启动序列结束之后长期运行 —— 它们必须比 RunStartupSequence()
// 活得久。放在本结构里（留在任务栈上）是安全的：任务永不返回，栈帧不会失效。
struct StartupContext {
  pipeline::WeatherSync sync;
  net::WifiStation wifi;
  scheduler::AppScheduler scheduler;
  bool portal_mode = false;

  explicit StartupContext(const pipeline::WeatherApiConfig& api_config)
      : sync(g_weather_store, api_config), scheduler(&UptimeMs) {}
};

// 一次性阶段：联网/门户 → 对时 → 定位 → 同步天气 → 注册并封存周期任务 →
// 请求进入正式页面。
void RunStartupSequence(StartupContext& ctx) {
  // 局部别名：把上下文成员绑定成引用，下面的周期任务回调按引用捕获它们。
  pipeline::WeatherSync& sync = ctx.sync;
  net::WifiStation& wifi = ctx.wifi;
  scheduler::AppScheduler& scheduler = ctx.scheduler;
  bool& portal_mode = ctx.portal_mode;

  ESP_LOGI(kTag, "[net] 启动 SNTP");
  // 候选列表与时区都在 main/include/time_config.h（可用 time_config_local.h
  // 整体覆盖）。时区是设备销售地区的属性，不是用户设置。
  net::StartSntp(
      {kNtpServers, std::size(kNtpServers), kUtcOffsetSeconds, kTzName});

  // 是否需要配网：没有配置就直接进；有配置但连不上会**回退**到配网。
  // 不这样做的话，用户密码输错一次就会永远卡在重试循环里，
  // 除了恢复出厂没有别的出路。
  portal_mode = !config::IsWifiConfigured(g_device_config);

  if (portal_mode) {
    ESP_LOGW(kTag, "[net] 未配置 Wi-Fi，启动强制门户");
    g_weather_store.MarkFailed(weather::DataSource::kCurrentConditions,
                               "Waiting for setup", UptimeMs());
    StartPortal();
  } else {
    app::GetStartupStatus().Set(app::StartupPhase::kConnectingWifi,
                                g_device_config.wifi_ssid);
    if (wifi.Begin() != ESP_OK) {
      ESP_LOGE(kTag, "[net] Wi-Fi 初始化失败");
      app::GetStartupStatus().Set(app::StartupPhase::kFailed,
                                  "Wi-Fi init failed");
      portal_mode = true;
      StartPortal();
    } else {
      ESP_LOGI(kTag, "[net] 连接 Wi-Fi \"%s\" ...",
               g_device_config.wifi_ssid.c_str());
      if (!wifi.Connect(g_device_config.wifi_ssid,
                        g_device_config.wifi_password, 20000)) {
        // 连不上 -> 转配网，让用户改密码/换网络。保存后设备会自动重启。
        ESP_LOGW(kTag, "[net] Wi-Fi 连接失败，转入配网模式");
        g_weather_store.MarkFailed(weather::DataSource::kCurrentConditions,
                                   "Wi-Fi connect failed", UptimeMs());
        app::GetStartupStatus().Set(
            app::StartupPhase::kFailed,
            "Wi-Fi connect failed: " + g_device_config.wifi_ssid);
        portal_mode = true;
        StartPortal();
      }
    }
  }

  if (!portal_mode) {
    app::GetStartupStatus().Set(app::StartupPhase::kSyncingTime);
    net::WaitForTime(15000);

    app::GetStartupStatus().Set(app::StartupPhase::kSyncingWeather);
    if (!sync.EnsureLocation(g_device_config)) {
      ESP_LOGW(kTag, "[net] 无法确定位置，天气同步将不可用");
    }
  }

  // ---------------- 周期任务：启动期一次性注册，然后封存 ----------------
  //
  // 注册必须全部发生在启动阶段，随后 Seal()，运行期注册一律被拒：
  // 注册若同时来自 UI 任务与 net 任务，就会出现"一边往容器里加、
  // 一边在 RunDue 里迭代"的并发访问，直接崩溃。
  //
  // 注意：背光的光敏自适应任务也必须注册在**这里**，绝不能在 UI 任务里 ——
  // 那样就又变成两个任务并发注册了。

  // 配网模式下没有网络，注册天气任务只会不断失败，因此跳过
  if (!portal_mode) {
    scheduler.Add(
        "weather_now", kWeatherSyncIntervalMs,
        [&sync]() { sync.SyncCurrentConditions(sync.LocationId()); },
        /*run_now=*/true);
    scheduler.Add(
        "weather_7d", kWeather7dIntervalMs,
        [&sync]() { sync.SyncDailyForecast(sync.LocationId()); },
        /*run_now=*/true);
    scheduler.Add(
        "air_quality", kAirQualitySyncIntervalMs,
        [&sync]() { sync.SyncAirQuality(sync.Latitude(), sync.Longitude()); },
        /*run_now=*/true);
  }
  // 背光自适应：**注册在启动期**，由本任务周期执行。
  //
  // 注册必须发生在 Seal() 之前：运行期注册会与 RunDue 的迭代并发，
  // 容器的插入与迭代同时进行就是崩溃。
  scheduler.Add(
      "backlight", kBacklightIntervalMs,
      []() {
        uint16_t raw = 0;
        uint8_t brightness = g_ambient_brightness.load();
        if (g_light_sensor.ReadRaw(raw)) {
          brightness = backlight::RawToBrightness(raw);
        }
        g_ambient_brightness.store(brightness);

        const auto mode =
            static_cast<backlight::BrightnessMode>(g_brightness_mode.load());
        g_backlight.SetDuty(backlight::ResolveDuty(mode, brightness));
      },
      /*run_now=*/true);
  // 通配 DNS 轮询。配网模式下驱动强制门户；非配网模式下 Poll() 内部立即返回。
  // 与其他任务一样在**启动期**注册 —— 见 CONVENTIONS §6.1。
  scheduler.Add(
      "dns_hijack", kDnsPollIntervalMs, []() { g_portal.Poll(); },
      /*run_now=*/false);

  // 断线重连任务**只在 STA 模式注册**。
  //
  // 配网模式下设备是 AP，wifi 对象根本没 Begin() 过；若照旧注册，
  // 这个任务会反复调用未初始化的 wifi.Connect()，且在 AP 模式下
  // net::IsNetworkUp() 恒为 false，等于每隔几秒空跑一次连接尝试。
  if (!portal_mode) {
    scheduler.Add(
        "network_watch", kNetworkWatchIntervalMs,
        [&wifi]() {
          if (net::IsNetworkUp()) {
            return;
          }
          ESP_LOGW(kTag, "[net] 网络断开，尝试重连");
          wifi.Connect(g_device_config.wifi_ssid, g_device_config.wifi_password,
                       20000);
        },
        /*run_now=*/false);
  }
  scheduler.Seal();

  if (scheduler.RejectedRegistrationCount() != 0) {
    ESP_LOGE(kTag, "[net] 有 %u 个周期任务注册被拒，请检查注册参数",
             static_cast<unsigned>(scheduler.RejectedRegistrationCount()));
  }
  ESP_LOGI(kTag, "[net] 周期任务已封存，共 %u 个",
           static_cast<unsigned>(scheduler.TaskCount()));

  // ---------------- 进入正式页面 ----------------
  // 决策在 startup_flow，实际切页由注入的回调完成 ——
  // 本模块因此不需要知道任何 UI / LVGL 细节。
  EnterInitialPage(portal_mode, g_on_navigate);
}

// 常驻阶段：调度器心跳。
//
// **不拆成第二个任务**：每个任务要 12 KB 栈，ESP32-C3 只有 400 KB SRAM；
// 而且周期任务注册时就绑定了本任务的栈上对象（sync / wifi），换任务会失效。
void RunSchedulerLoop(scheduler::AppScheduler& scheduler) {
  while (true) {
    scheduler.RunDue();
    vTaskDelay(pdMS_TO_TICKS(kSchedulerTickMs));
  }
}

void StartupTaskEntry(void* /*arg*/) {
  ESP_LOGI(kTag, "[net] 数据任务启动");

  // API 配置必须在构造 sync **之前**校验并告警：sync 是上下文成员，
  // 构造它就进入启动序列了，告警必须排在"数据任务启动"之后、"启动 SNTP"之前。
  const pipeline::WeatherApiConfig api_config{QWEATHER_API_BASE_URL,
                                              QWEATHER_API_KEY};
  if (!api_config.IsConfigured()) {
    ESP_LOGW(kTag,
             "[net] 天气 API 未配置（缺少 weather_config_local.h），"
             "天气同步将不可用");
  }

  StartupContext ctx(api_config);
  RunStartupSequence(ctx);
  RunSchedulerLoop(ctx.scheduler);
}

}  // namespace

void StartStartupTask(NavigationRequest on_navigate) {
  g_on_navigate = on_navigate;
  xTaskCreate(StartupTaskEntry, "StartupTask", kStartupTaskStack, nullptr,
              kStartupTaskPriority, nullptr);
}

}  // namespace dida
