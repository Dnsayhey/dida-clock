#!/usr/bin/env bash
#
# 主机单测：构建并运行 dida-clock 的纯逻辑测试。
#
# 不需要开发板，也不需要 ESP-IDF 环境 —— 直接用宿主编译器编译。
# cJSON 源码复用 IDF 组件管理器下载到 managed_components 的那一份，
# 保证主机测试与固件用的是同一个 JSON 实现。
#
# 用法：
#   ./run_tests.sh          # 普通构建 + 运行
#   ./run_tests.sh --tsan   # 额外用 ThreadSanitizer 构建 + 运行

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

CJSON_DIR="${PROJECT_ROOT}/managed_components/espressif__cjson/cJSON"
if [[ ! -f "${CJSON_DIR}/cJSON.c" ]]; then
  echo "错误：找不到 cJSON 源码于 ${CJSON_DIR}" >&2
  echo "请先在项目根目录执行一次 'idf.py build' 以拉取托管组件。" >&2
  exit 1
fi

CXX="${CXX:-clang++}"
CXXFLAGS=(-std=c++20 -g -O1 -Wall -Wextra -Wno-unused-parameter
          "-DFIXTURES_DIR=\"${SCRIPT_DIR}/fixtures\""
          "-I${PROJECT_ROOT}/components/weather/include"
          "-I${PROJECT_ROOT}/components/store/include"
          "-I${PROJECT_ROOT}/components/decompress/include"
          "-I${PROJECT_ROOT}/components/storage/include"
          "-I${PROJECT_ROOT}/components/scheduler/include"
          "-I${PROJECT_ROOT}/components/net/include"
          "-I${PROJECT_ROOT}/components/app/include"
          "-I${PROJECT_ROOT}/components/input/include"
          "-I${PROJECT_ROOT}/components/backlight/include"
          "-I${PROJECT_ROOT}/components/portal/include"
          "-I${CJSON_DIR}"
          "-I${SCRIPT_DIR}")

# 只编译纯逻辑部分：decompress 的设备侧实现（decompress_miniz.cpp）依赖
# ESP32 ROM 的 miniz，主机上不可用；本套件改为用系统 zlib 作为参考实现
# 来验证 gzip 头解析结果是否正确（见 test_decompress.cpp）。
SOURCES=(
  "${PROJECT_ROOT}/components/weather/weather_parser.cpp"
  "${PROJECT_ROOT}/components/weather/weather_api.cpp"
  "${PROJECT_ROOT}/components/weather/weather_types.cpp"
  "${PROJECT_ROOT}/components/weather/weather_category.cpp"
  "${PROJECT_ROOT}/components/store/weather_store.cpp"
  "${PROJECT_ROOT}/components/decompress/decompress_format.cpp"
  "${PROJECT_ROOT}/components/storage/device_config.cpp"
  "${PROJECT_ROOT}/components/scheduler/app_scheduler.cpp"
  "${PROJECT_ROOT}/components/net/time_zone.cpp"
  "${PROJECT_ROOT}/components/app/app_controller.cpp"
  "${PROJECT_ROOT}/components/app/app_command_executor.cpp"
  "${PROJECT_ROOT}/components/app/app_events.cpp"
  "${PROJECT_ROOT}/components/app/page_type.cpp"
  "${PROJECT_ROOT}/components/app/settings_view_data.cpp"
  "${PROJECT_ROOT}/components/app/weather_view_data.cpp"
  "${PROJECT_ROOT}/components/app/runtime_status.cpp"
  "${PROJECT_ROOT}/components/backlight/backlight_policy.cpp"
  "${PROJECT_ROOT}/components/portal/portal_page.cpp"
  "${PROJECT_ROOT}/components/portal/dns_message.cpp"
  "${PROJECT_ROOT}/components/input/button_event.cpp"
  "${PROJECT_ROOT}/components/input/button_event_detector.cpp"
  "${CJSON_DIR}/cJSON.c"
)

TESTS=(
  "${SCRIPT_DIR}/test_main.cpp"
  "${SCRIPT_DIR}/test_weather_parser.cpp"
  "${SCRIPT_DIR}/test_weather_category.cpp"
  "${SCRIPT_DIR}/test_weather_parser_real.cpp"
  "${SCRIPT_DIR}/test_weather_api.cpp"
  "${SCRIPT_DIR}/test_weather_store.cpp"
  "${SCRIPT_DIR}/test_decompress.cpp"
  "${SCRIPT_DIR}/test_device_config.cpp"
  "${SCRIPT_DIR}/test_app_scheduler.cpp"
  "${SCRIPT_DIR}/test_button_event_detector.cpp"
  "${SCRIPT_DIR}/test_app_navigation.cpp"
  "${SCRIPT_DIR}/test_backlight_policy.cpp"
  "${SCRIPT_DIR}/test_portal.cpp"
  "${SCRIPT_DIR}/test_weather_view_data.cpp"
  "${SCRIPT_DIR}/test_time_zone.cpp"
)

mkdir -p "${BUILD_DIR}"

build_and_run() {
  local label="$1"; shift
  local sanitize="$1"; shift
  local extra=("$@")

  local bin="${BUILD_DIR}/${label}"
  echo "============================================================"
  echo "构建: ${label}"
  echo "============================================================"
  # shellcheck disable=SC2086
  "${CXX}" "${CXXFLAGS[@]}" ${sanitize} \
      "${SOURCES[@]}" "${TESTS[@]}" -pthread -lz -o "${bin}"
  echo "运行:"
  echo
  "${bin}"
}

build_and_run "test_suite" ""

# 方法学自证：证明"全字段一致性"检测确实能抓出无锁实现的撕裂，
# 否则主套件通过并不能说明修复有效。
echo
echo "============================================================"
echo "方法学自证: race_demo"
echo "============================================================"
"${CXX}" "${CXXFLAGS[@]}" \
    "${SOURCES[@]}" "${SCRIPT_DIR}/race_demo.cpp" -pthread \
    -o "${BUILD_DIR}/race_demo"

echo
echo "============================================================"
echo "方法学自证: scheduler_race_demo"
echo "============================================================"
"${CXX}" "${CXXFLAGS[@]}" \
    "${SOURCES[@]}" "${SCRIPT_DIR}/scheduler_race_demo.cpp" -pthread \
    -o "${BUILD_DIR}/scheduler_race_demo"
"${BUILD_DIR}/scheduler_race_demo"
"${BUILD_DIR}/race_demo"

echo
echo "============================================================"
echo "方法学自证: scheduler_race_demo"
echo "============================================================"
"${CXX}" "${CXXFLAGS[@]}" \
    "${SOURCES[@]}" "${SCRIPT_DIR}/scheduler_race_demo.cpp" -pthread \
    -o "${BUILD_DIR}/scheduler_race_demo"
"${BUILD_DIR}/scheduler_race_demo"

if [[ "${1:-}" == "--tsan" ]]; then
  echo
  echo "============================================================"
  echo "ThreadSanitizer 校验"
  echo "============================================================"

  # 1) 主套件在 TSan 下必须**零**数据竞争警告
  "${CXX}" "${CXXFLAGS[@]}" -fsanitize=thread \
      "${SOURCES[@]}" "${TESTS[@]}" -pthread -lz \
      -o "${BUILD_DIR}/test_suite_tsan" >/dev/null
  set +e
  "${BUILD_DIR}/test_suite_tsan" >"${BUILD_DIR}/tsan_suite.log" 2>&1
  suite_rc=$?
  set -e
  suite_races="$(grep -c 'WARNING: ThreadSanitizer' "${BUILD_DIR}/tsan_suite.log" || true)"

  echo
  echo "主套件 (test_suite_tsan):"
  grep -E '用例:|全部通过|存在失败|撕裂' "${BUILD_DIR}/tsan_suite.log" | sed 's/^/    /'
  echo "    TSan 警告数: ${suite_races}  (期望 0)"
  echo "    退出码: ${suite_rc}  (期望 0)"

  # 2) 无锁实现必须被 TSan **抓到**，否则说明校验手段无效
  "${CXX}" "${CXXFLAGS[@]}" -fsanitize=thread \
      "${SOURCES[@]}" "${SCRIPT_DIR}/race_demo.cpp" -pthread \
      -o "${BUILD_DIR}/race_demo_tsan" >/dev/null
  set +e
  # halt_on_error=0：让程序跑完并自行打印结论，而不是被 TSan 中断
  TSAN_OPTIONS=halt_on_error=0 "${BUILD_DIR}/race_demo_tsan" \
      >"${BUILD_DIR}/tsan_race_demo.log" 2>&1
  set -e
  demo_races="$(grep -c 'WARNING: ThreadSanitizer' "${BUILD_DIR}/tsan_race_demo.log" || true)"

  echo
  echo "方法学自证 (race_demo_tsan):"
  grep -E '读取 [0-9]+ 次|检测手段有效|修复有效' "${BUILD_DIR}/tsan_race_demo.log" | sed 's/^/    /'
  echo "    TSan 警告数: ${demo_races}  (期望 > 0，全部针对无锁实现)"

  # 3) 调度器的无锁对照也必须在 TSan 下被检出内存竞争
  "${CXX}" "${CXXFLAGS[@]}" -fsanitize=thread \
      "${SOURCES[@]}" "${SCRIPT_DIR}/scheduler_race_demo.cpp" -pthread \
      -o "${BUILD_DIR}/scheduler_race_demo_tsan" >/dev/null
  set +e
  TSAN_OPTIONS=halt_on_error=0 "${BUILD_DIR}/scheduler_race_demo_tsan" \
      >"${BUILD_DIR}/tsan_scheduler_demo.log" 2>&1
  set -e
  sched_races="$(grep -c 'WARNING: ThreadSanitizer' "${BUILD_DIR}/tsan_scheduler_demo.log" || true)"

  echo
  echo "调度器方法学自证 (scheduler_race_demo_tsan):"
  grep -E '注册被接受|负对照被暴露|封存版拒绝' "${BUILD_DIR}/tsan_scheduler_demo.log" | sed 's/^/    /'
  echo "    TSan 警告数: ${sched_races}  (期望 > 0，全部针对无锁实现)"

  echo
  echo "------------------------------------------------------------"
  ok=1
  if [[ "${suite_races}" != "0" || "${suite_rc}" != "0" ]]; then
    ok=0; echo "  主套件未通过 TSan 校验"
  fi
  if [[ "${demo_races}" -le 0 ]]; then
    ok=0; echo "  无锁实现未被 TSan 抓到，校验手段无效"
  fi
  if [[ "${sched_races}" -le 0 ]]; then
    ok=0; echo "  无锁调度器未被 TSan 抓到，校验手段无效"
  fi
  if [[ "${ok}" == "1" ]]; then
    echo "  TSan 校验通过：修复后的实现零竞争，且校验手段确实能抓出无锁实现"
  fi
  exit $(( ok == 1 ? 0 : 1 ))
fi

