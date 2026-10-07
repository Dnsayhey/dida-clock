#!/usr/bin/env bash
#
# 约定检查：把 CONVENTIONS.md 里"写下来的规则"变成**可执行的检查**，防止退化。
#
# 用法：
#   scripts/check_conventions.sh          # 全部检查
#   scripts/check_conventions.sh --fix    # 顺便用 clang-format 修正格式
#
# 退出码 0 = 全部通过。

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT}"

FIX=0
[[ "${1:-}" == "--fix" ]] && FIX=1

failures=0
section() { printf '\n\033[1m%s\033[0m\n' "$1"; }
ok()      { printf '  \033[32m✓\033[0m %s\n' "$1"; }
bad()     { printf '  \033[31m✗\033[0m %s\n' "$1"; failures=$((failures + 1)); }

# ---------------------------------------------------------------------------
section "1. 可主机测试的文件不得依赖 ESP-IDF"
# ---------------------------------------------------------------------------
# 单一事实来源：run_tests.sh 的 SOURCES 列表就是"主机可编译"的清单。
# 这些文件一旦引入 esp_ 头，就再也无法在主机上测试 —— 是架构退化。
#
# 注意必须做**递归 include 闭包**扫描：只查 .cpp 本身是不够的，
# 它包含的项目头文件里一样可能引入 esp_ 头。
if ! python3 - "${ROOT}" <<'PYEOF'
import glob, os, re, sys

root = sys.argv[1]
runner = os.path.join(root, "tests/host/run_tests.sh")

# 从 run_tests.sh 解析出主机可编译的源文件清单
sources = []
text = open(runner, encoding="utf-8").read()
block = re.search(r"^SOURCES=\((.*?)^\)", text, re.S | re.M)
if not block:
    print("  \033[31m✗\033[0m 无法从 run_tests.sh 解析 SOURCES")
    sys.exit(1)
for line in block.group(1).splitlines():
    m = re.search(r'\$\{PROJECT_ROOT\}/([^"]+)', line)
    if m:
        sources.append(os.path.join(root, m.group(1)))

INC_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)
ESP_RE = re.compile(r'^\s*#\s*include\s*[<"]esp_', re.M)

seen, violations = set(), []

def scan(path, chain):
    path = os.path.normpath(path)
    if path in seen or not os.path.isfile(path):
        return
    seen.add(path)
    body = open(path, encoding="utf-8", errors="ignore").read()

    if ESP_RE.search(body):
        violations.append((path, chain))

    # 跟进项目内的头文件。
    # 路径解析必须考虑本项目的布局：include 路径带组件名前缀，
    # 实际文件在 components/<组件>/include/<组件>/... 下。
    for inc in INC_RE.findall(body):
        candidates = (
            glob.glob(os.path.join(root, "components", "*", "include", inc))
            + glob.glob(os.path.join(root, "main", "include", inc))
            + glob.glob(os.path.join(root, "main", inc))
        )
        for cand in candidates:
            scan(cand, chain + [os.path.relpath(cand, root)])
            break

for src in sources:
    scan(src, [os.path.relpath(src, root)])

if violations:
    print("  \033[31m✗\033[0m 以下主机可测文件的 include 闭包引入了 ESP-IDF 头：")
    for path, chain in violations:
        print("    " + os.path.relpath(path, root))
        for step in chain[:4]:
            print("      <- " + step)
    sys.exit(1)

print(f"  \033[32m✓\033[0m 主机测试文件（含 include 闭包，共 {len(seen)} 个头/源文件）均未引入 ESP-IDF 头")
PYEOF
then
  failures=$((failures + 1))
fi

# ---------------------------------------------------------------------------
section "2. 页面层不得依赖底层服务"
# ---------------------------------------------------------------------------
# pages/ 只能依赖 app/ 提供的 provider —— 这是"数据与渲染解耦"的核心约束。
layer_violations=$(grep -rnE '#include\s*[<"](net|storage|weather|store)/' \
                     components/pages 2>/dev/null || true)
if [[ -n "${layer_violations}" ]]; then
  bad "components/pages 直接依赖了底层服务："
  printf '%s\n' "${layer_violations}" | sed 's|^|    |'
else
  ok "components/pages 未直接依赖 net/storage/weather/store"
fi

# ---------------------------------------------------------------------------
section "3. 代码格式（clang-format）"
# ---------------------------------------------------------------------------
if ! command -v clang-format >/dev/null 2>&1; then
  printf '  \033[33m!\033[0m 未安装 clang-format，跳过格式检查\n'
else
  fmt_files=$(find components main tests -name '*.cpp' -o -name '*.h')
  if [[ "${FIX}" == "1" ]]; then
    # shellcheck disable=SC2086
    clang-format -i ${fmt_files}
    ok "已用 clang-format 就地修正"
  fi
  drift=0
  drifted=""
  for f in ${fmt_files}; do
    if ! clang-format --dry-run --Werror "$f" >/dev/null 2>&1; then
      drift=$((drift + 1))
      drifted+="    $f"$'\n'
    fi
  done
  if [[ "${drift}" -gt 0 ]]; then
    bad "${drift} 个文件格式不符（运行 '$0 --fix' 修正）："
    printf '%s' "${drifted}"
  else
    ok "全部文件格式符合 .clang-format"
  fi
fi

# ---------------------------------------------------------------------------
section "4. 函数命名（PascalCase）"
# ---------------------------------------------------------------------------
# 只检查头文件里的成员/自由函数声明，避免误伤局部变量与标准库调用。
naming_violations=$(grep -rnE '^\s+(static\s+)?(esp_err_t|void|bool|int|std::string|std::size_t|uint8_t|uint32_t|uint64_t|const char\*|lv_obj_t\*|const std::string&|const lv_obj_t\*|std::string&)\s+[a-z][A-Za-z0-9_]*\s*\(' \
                      components/*/include/*/*.h 2>/dev/null |
                    grep -v 'operator()' || true)
if [[ -n "${naming_violations}" ]]; then
  bad "以下声明不是 PascalCase："
  printf '%s\n' "${naming_violations}" | sed 's|^|    |'
else
  ok "头文件中的函数声明均为 PascalCase"
fi

# ---------------------------------------------------------------------------
section "5. 页面不得硬编码颜色"
# ---------------------------------------------------------------------------
# 页面只能通过 ui_theme 的角色（ApplyTextRole / ApplyThemeToContainer）取色。
# 直接写死颜色会在换主题时失效 —— 实机就踩过：CreateFullScreenRoot() 里写死
# lv_color_black()，浅色主题下变成"黑底黑字"，整页除灰字提示外全部不可见。
#
# 例外：
#   ui_theme.cpp —— 配色表的定义处，本来就该有具体色值
hardcoded=$(grep -rnE 'lv_color_(black|white)\(|lv_color_hex\(0x|lv_palette_(main|lighten|darken)\(' \
              components/pages/*.cpp 2>/dev/null |
            grep -v 'components/pages/ui_theme.cpp' |
            grep -vE ':[0-9]+:\s*//' || true)
if [[ -n "${hardcoded}" ]]; then
  bad "以下位置硬编码了颜色（应改用 ui_theme 的角色）："
  printf '%s\n' "${hardcoded}" | sed 's|^|    |'
else
  ok "页面均通过 ui_theme 取色，无硬编码"
fi

# ---------------------------------------------------------------------------
section "6. 字库覆盖源码用字"
# ---------------------------------------------------------------------------
# 字库是编译期固定的，而文案是源码里的字符串。改了文案忘了重新生成字库，
# 缺的字在屏幕上就是空白/方框 —— 而**构建、单测都会通过**，只有烧到板子上
# 用眼睛看才发现。所以这一步必须自动守。
#
# 先自检提取器本身：它决定"哪些字算需要字形"。日志/断言的排除一旦越界，
# 覆盖检查会**静默地少要求**字形，症状同样是"只有实机才看得见"。
if python3 "${SCRIPT_DIR}/extract_cjk.py" --selftest > /tmp/dida_cjk_selftest.txt 2>&1; then
  ok "字库用字提取器自检通过"
else
  bad "字库用字提取器自检失败（排除范围可能越界）："
  sed 's/^/    /' /tmp/dida_cjk_selftest.txt
fi

if python3 "${SCRIPT_DIR}/check_fonts.py" > /tmp/dida_font_check.txt 2>&1; then
  ok "字库覆盖源码用到的全部汉字"
else
  bad "字库缺字（运行 scripts/gen_fonts.py 重新生成）："
  sed 's/^/    /' /tmp/dida_font_check.txt | tail -n +2
fi

# ---------------------------------------------------------------------------
printf '\n============================================================\n'
if [[ "${failures}" -eq 0 ]]; then
  printf '\033[32m约定检查全部通过\033[0m\n'
  exit 0
fi
printf '\033[31m约定检查失败：%d 项\033[0m\n' "${failures}"
exit 1
