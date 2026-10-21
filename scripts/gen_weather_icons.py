#!/usr/bin/env python3
"""生成实时天气页的 70 个 QWeather 天气图标（A4 掩码）。

为什么是 A4 掩码而不是图标字体
------------------------------
本页面只需要一个 40x40 的天气图标。A4 每像素 4 bit，40x40 一个图标 800 B，
70 个约 56 KB；A4 只带透明度，LVGL 用 `image_recolor` 上色，一套素材同时
覆盖深色/浅色主题。图形本身来自和风天气官方 SVG，线条风格与 A1 太空人一致。

为什么用 Chrome 栅格化
----------------------
项目里没有 Pillow / librsvg / ImageMagick。用一个 HTML 雪碧图 + Chrome
headless 截图，能直接得到带 alpha 的 RGBA PNG，再用项目已有的 zlib PNG
解码器（scripts/gen_astronaut.py）读出 alpha。这样不引入新依赖。

输出
----
components/assets/image/dida_weather_icons.c
  * 70 个 40x40 的 A4 位图
  * dida_weather_icons[] / dida_weather_icon_codes[] / dida_weather_icon_count
  * 页面通过 assets/weather_icons.h 的 assets::WeatherIcon(code) 查表

用法
----
python3 scripts/gen_weather_icons.py
# 可选：QWEATHER_ICONS_DIR=/path/to/qweather-icons/package
# 不提供时自动 npm pack qweather-icons@1.8.0 到临时目录。
"""

import json
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import gen_astronaut  # noqa: E402  (复用它的 PNG 解码器)

VERSION = "1.8.0"
SIZE = 40
SCALE = 4  # 先按 160x160 渲染，再 4x4 平均下采样到 40x40
CELL = SIZE * SCALE
COLS = 10
OUT_C = ROOT / "components" / "assets" / "image" / "dida_weather_icons.c"

CHROME_CANDIDATES = (
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
)


def find_chrome():
    for path in CHROME_CANDIDATES:
        if os.access(path, os.X_OK):
            return path
    for name in ("google-chrome", "chromium", "chromium-browser"):
        path = shutil.which(name)
        if path:
            return path
    raise SystemExit("找不到 Chrome / Chromium，无法把 SVG 栅格化")


def find_package():
    env = os.environ.get("QWEATHER_ICONS_DIR")
    if env:
        package = pathlib.Path(env)
        if not (package / "font" / "qweather-icons.json").is_file():
            raise SystemExit(f"QWEATHER_ICONS_DIR 下缺少 font/qweather-icons.json: {package}")
        return package

    tmp = pathlib.Path(tempfile.mkdtemp(prefix="qweather-icons-"))
    subprocess.run(
        ["npm", "pack", f"qweather-icons@{VERSION}", "--silent"],
        cwd=tmp,
        check=True,
        stdout=subprocess.DEVNULL,
    )
    archives = sorted(tmp.glob("*.tgz"))
    if not archives:
        raise SystemExit("npm pack 没有生成 .tgz")
    with tarfile.open(archives[0]) as tar:
        tar.extractall(tmp)
    package = tmp / "package"
    if not (package / "font" / "qweather-icons.json").is_file():
        raise SystemExit(f"解包后缺少 font/qweather-icons.json: {package}")
    return package


def load_codes(package):
    meta = json.loads((package / "font" / "qweather-icons.json").read_text())
    # 天气/夜间/月相/热冷未知等 100..999；预警 1001+ 不需要
    return sorted(int(k) for k in meta if k.isdigit() and int(k) <= 999)


def build_sheet(package, codes, out_html, out_png):
    rows = (len(codes) + COLS - 1) // COLS
    parts = [
        "<!doctype html><html><head><meta charset=\"utf-8\"><style>",
        "html,body{margin:0;background:transparent;}",
        f"body{{display:grid;grid-template-columns:repeat({COLS},{CELL}px);}}",
        f".cell{{width:{CELL}px;height:{CELL}px;color:#000;}}",
        f".cell svg{{width:{CELL}px;height:{CELL}px;display:block;}}",
        "</style></head><body>",
    ]
    for code in codes:
        svg = (package / "icons" / f"{code}.svg").read_text()
        parts.append(f'<div class="cell">{svg}</div>')
    parts.append("</body></html>")
    out_html.write_text("".join(parts))

    chrome = find_chrome()
    subprocess.run(
        [
            chrome,
            "--headless",
            "--disable-gpu",
            "--hide-scrollbars",
            "--default-background-color=00000000",
            "--force-device-scale-factor=1",
            f"--screenshot={out_png}",
            f"--window-size={COLS * CELL},{rows * CELL}",
            out_html.as_uri(),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return rows


def downsample(png_path, rows, codes):
    width, height, channels, pixels = gen_astronaut.decode_png(str(png_path))
    if channels != 4:
        raise SystemExit(f"Chrome 截图不是 RGBA（channels={channels}）")
    if (width, height) != (COLS * CELL, rows * CELL):
        raise SystemExit(f"截图尺寸不对: {width}x{height}")

    alpha = []
    for idx, code in enumerate(codes):
        cx = (idx % COLS) * CELL
        cy = (idx // COLS) * CELL
        values = []
        for y in range(SIZE):
            for x in range(SIZE):
                total = 0
                for dy in range(SCALE):
                    base = ((cy + y * SCALE + dy) * width + cx + x * SCALE) * 4
                    for dx in range(SCALE):
                        total += pixels[base + dx * 4 + 3]
                values.append((total + (SCALE * SCALE) // 2) // (SCALE * SCALE))
        alpha.append((code, values))
    return alpha


def a4_rows(values):
    """按 LVGL A4 的约定打包：每字节高半字节是前一个像素。"""
    rows = []
    for y in range(SIZE):
        row = []
        for x in range(0, SIZE, 2):
            first = (values[y * SIZE + x] * 15 + 127) // 255
            second = (values[y * SIZE + x + 1] * 15 + 127) // 255
            row.append((first << 4) | second)
        rows.append(row)
    return rows


def write_c(icons):
    parts = [
        "// 由 scripts/gen_weather_icons.py 生成，请勿手工修改。",
        "//",
        f"// QWeather Icons v{VERSION}，{SIZE}x{SIZE}，A4（4 bit alpha）。",
        "// 图标 © QWeather，CC BY 4.0；来源与改动见 components/assets/icons/NOTICE.txt。",
        "// 每个图标只有透明度，页面用 image_recolor 上色，一套素材覆盖深浅主题。",
        "",
        '#include "lvgl.h"',
        "",
    ]
    refs = []
    for code, values in icons:
        rows = a4_rows(values)
        name = f"wx_{code}"
        parts.append(f"static const uint8_t {name}_map[{SIZE * SIZE // 2}] = {{")
        for row in rows:
            parts.append("    " + ", ".join(f"0x{b:02X}" for b in row) + ",")
        parts.append("};")
        parts.append("")
        parts.append(f"static const lv_image_dsc_t {name} = {{")
        parts.append("    .header = {")
        parts.append("        .magic = LV_IMAGE_HEADER_MAGIC,")
        parts.append("        .cf = LV_COLOR_FORMAT_A4,")
        parts.append("        .flags = 0,")
        parts.append(f"        .w = {SIZE},")
        parts.append(f"        .h = {SIZE},")
        parts.append(f"        .stride = {SIZE // 2},")
        parts.append("        .reserved_2 = 0,")
        parts.append("    },")
        parts.append(f"    .data_size = {SIZE * SIZE // 2},")
        parts.append(f"    .data = {name}_map,")
        parts.append("    .reserved = nullptr,")
        parts.append("    .reserved_2 = nullptr,")
        parts.append("};")
        parts.append("")
        refs.append(f"    &{name},")

    parts.append("const lv_image_dsc_t* const dida_weather_icons[] = {")
    parts.extend(refs)
    parts.append("};")
    parts.append("")
    parts.append("const uint16_t dida_weather_icon_codes[] = {")
    for i in range(0, len(icons), 10):
        chunk = icons[i : i + 10]
        parts.append("    " + ", ".join(str(code) for code, _ in chunk) + ",")
    parts.append("};")
    parts.append("")
    parts.append(f"const unsigned dida_weather_icon_count = {len(icons)};")
    parts.append("")
    OUT_C.write_text("\n".join(parts))


def main():
    package = find_package()
    codes = load_codes(package)
    if len(codes) != 70:
        print(f"警告：预期 70 个天气代码，实际 {len(codes)}", file=sys.stderr)

    tmp = pathlib.Path(tempfile.mkdtemp(prefix="dida-weather-icons-"))
    html = tmp / "icons.html"
    png = tmp / "icons.png"
    rows = build_sheet(package, codes, html, png)
    icons = downsample(png, rows, codes)

    empty = [code for code, values in icons if not any(values)]
    if empty:
        raise SystemExit(f"以下图标栅格化后为空: {empty}")

    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    write_c(icons)
    total = len(icons) * SIZE * SIZE // 2
    print(f"已生成 {OUT_C}")
    print(f"  {len(icons)} 个图标，{SIZE}x{SIZE} A4，共 {total} 字节"
          f"（{total / 1024:.1f} KB）")


if __name__ == "__main__":
    main()
