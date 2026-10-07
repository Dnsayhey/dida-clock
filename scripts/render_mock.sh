#!/usr/bin/env bash
#
# 渲染界面原型：把一份 HTML 原型渲染成 PNG，供人眼看、也供 AI 看。
#
# 用法：
#   ./scripts/render_mock.sh 原型.html [输出.png] [窗口尺寸]
#
#   ./scripts/render_mock.sh /tmp/mock/rt.html
#   ./scripts/render_mock.sh /tmp/mock/rt.html /tmp/mock/rt.png 1080x400
#
# 为什么需要这个脚本
# ------------------
# 界面改动（尤其配色与间距）在**文字描述里无法判断**，必须看到渲染结果。
# 但手写 Chrome 命令行有两个坑：
#
#   1. **必须用 `--force-device-scale-factor=2`**。
#      常见的错法是先按 1x 截图、再用 HTML 把 `<img>` 放大到 2 倍 —— 那只是
#      插值，边缘照样是糊的。设备是 240x320 的小屏，字体只有 16~70px，
#      1x 截图里 1 个像素的差别（正是配色和间距的判断依据）会看不出来。
#
#   2. **窗口尺寸要显式给**。headless 不会自动裁剪到内容大小；给大了会留下
#      大片背景色，给小了会截断。原型页自己知道要画多大（见下），
#      调用方按它传即可。
#
# 原型页的约定（模板见 docs/界面原型渲染流程.md）
# ------------------------------------------------
#   * 一块「屏幕」是 `width:240px; height:320px` 的 div，尺寸与
#     components/pages/include/pages/page_layout.h 里的
#     kScreenWidth / kScreenHeight 一致。
#   * 每个元素用 `position:absolute` + `left/top` **直接写 LVGL 的坐标**，
#     不靠 flex 布局 —— 这样原型与代码是 1:1 的，改了哪一行能对上。
#   * 多个方案并排放在一个 `display:flex` 的行里，每个上面加一行标题。
#
# 渲染完用 read_image 工具把 PNG 读出来看 —— 这一步不能省，
# 「渲染了」不等于「看过了」。

set -euo pipefail

if [ $# -lt 1 ]; then
  echo "用法: $0 <原型.html> [输出.png] [窗口尺寸，如 1080x400]" >&2
  exit 2
fi

HTML="$1"
OUT="${2:-${HTML%.html}.png}"
SIZE="${3:-1080x380}"

[ -f "$HTML" ] || { echo "找不到输入文件: $HTML" >&2; exit 1; }

# 输出路径转成绝对路径 —— Chrome 会以它自己的工作目录解析相对路径
case "$OUT" in
  /*) ;;
  *) OUT="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")" ;;
esac

# 找 Chrome。macOS 上装法不唯一，按可能性依次找。
CHROME=""
for c in \
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
  "/Applications/Chromium.app/Contents/MacOS/Chromium" \
  "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge" \
  "$(command -v google-chrome 2>/dev/null || true)" \
  "$(command -v chromium 2>/dev/null || true)"
do
  if [ -n "$c" ] && [ -x "$c" ]; then CHROME="$c"; break; fi
done

if [ -z "$CHROME" ]; then
  echo "找不到 Chrome / Chromium。请安装 Google Chrome，或改用其它无头浏览器截图。" >&2
  exit 1
fi

# 窗口尺寸 'WxH' -> Chrome 要的 'W,H'
WINSIZE="${SIZE//x/,}"

"$CHROME" \
  --headless \
  --disable-gpu \
  --hide-scrollbars \
  --virtual-time-budget=2000 \
  --force-device-scale-factor=2 \
  --screenshot="$OUT" \
  --window-size="$WINSIZE" \
  "$HTML" >/dev/null 2>&1

if [ ! -f "$OUT" ]; then
  echo "渲染失败，没有生成 $OUT" >&2
  exit 1
fi

# 报告实际像素尺寸（应为请求尺寸的 2 倍）
DIMS="$(sips -g pixelWidth -g pixelHeight "$OUT" 2>/dev/null | awk '/pixel/{printf "%s ", $2}')"
echo "已渲染: $OUT  (${DIMS% })  <- 窗口 ${SIZE} 的 2 倍"
