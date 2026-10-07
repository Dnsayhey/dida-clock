#!/usr/bin/env python3
"""生成太空人动画的 LVGL 图像数据。

为什么不直接存 RGB565
----------------------
原素材（300x300 的太空人线稿）是**黑底白色线稿**，而且明暗两套只是同一张
图的反转。它不是照片，是二值图形 —— 用 RGB565 存 10 帧要 63 KB（明暗两套
125 KB），而按 1 位掩码存只要 3.9 KB，**省 16 倍**。

上色方式：A1 是"只有透明度、没有颜色"的格式。LVGL 在绘制时把
`sup.alpha_color = draw_dsc->recolor`（src/draw/lv_draw_image.c），
所以用 `lv_obj_set_style_image_recolor()` 就能把它染成主题色 ——
深色主题画白、浅色主题画黑，**一套素材覆盖两个主题**。

依赖：macOS 的 sips（JPEG -> PNG 缩放），Python 标准库 zlib（解 PNG）。
不需要 Pillow。

用法：python3 scripts/gen_astronaut.py
"""

import io
import os
import struct
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, "components", "assets", "astronaut_src")
OUT_C = os.path.join(ROOT, "components", "assets", "image", "dida_astro.c")

# 显示尺寸。版面里太空人占右侧 80px 宽，取 56x56 与左侧信息块视觉平衡。
SIZE = 56
# 亮度阈值。线稿是纯黑背景 + 纯白线条，取中值即可。
THRESHOLD = 128
# 下采样时，源块内白点比例达到这个百分比就判为白。
# 调低 -> 线条更粗更连贯；调高 -> 更细但可能断续。
COVERAGE_PERCENT = 18
# 下采样前的中间尺寸倍数。源图是 300x300，不是 SIZE 的整数倍，
# 所以先让 sips 缩到 SIZE*BLOCK（280x280），让每个输出像素恰好对应
# 一个 BLOCK x BLOCK 的源块 —— 这样覆盖率才有确定含义。
BLOCK = 5


def decode_png(path):
    """解出 PNG 的像素数据。

    只支持 8 位、非隔行、colortype 0/2/4/6（灰度/RGB/灰度+A/RGBA）——
    sips 的输出正好落在这里。返回 (width, height, channels, bytes)。
    """
    raw = io.open(path, "rb").read()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: 不是 PNG")
    pos, idat, header = 8, b"", None
    while pos < len(raw):
        length = struct.unpack(">I", raw[pos : pos + 4])[0]
        ctype = raw[pos + 4 : pos + 8]
        data = raw[pos + 8 : pos + 8 + length]
        if ctype == b"IHDR":
            w, h, depth, color, comp, filt, interlace = struct.unpack(
                ">IIBBBBB", data
            )
            if depth != 8 or interlace != 0:
                raise ValueError(f"{path}: 只支持 8 位非隔行 PNG")
            header = (w, h, color)
        elif ctype == b"IDAT":
            idat += data
        elif ctype == b"IEND":
            break
        pos += 12 + length
    if header is None:
        raise ValueError(f"{path}: 缺少 IHDR")
    w, h, color = header
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color)
    if channels is None:
        raise ValueError(f"{path}: 不支持的 colortype {color}")

    stride = w * channels
    decoded = zlib.decompress(idat)
    out = bytearray()
    prev = bytearray(stride)
    p = 0
    for _ in range(h):
        ftype = decoded[p]
        p += 1
        line = bytearray(decoded[p : p + stride])
        p += stride
        # PNG 的 5 种行滤波，逐字节还原
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 0xFF
            elif ftype == 2:
                line[i] = (line[i] + b) & 0xFF
            elif ftype == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        out += line
        prev = line
    return w, h, channels, bytes(out)


def to_a1_bits(png_path):
    """把一张图转成 1 位掩码的字节序列（每行 MSB 在前，行末补到整字节）。

    顺序是"先在原分辨率下二值化，再按覆盖率下采样"，而不是"先缩放再
    二值化"。原图是细白线，先缩放会把线稀释成灰、阈值化后断成虚线；
    先二值化则每个源像素非黑即白，下采样时只要块内有足够多的白点就
    判为白，细线得以保持连续。
    """
    w, h, channels, pixels = decode_png(png_path)
    if (w, h) != (SIZE * BLOCK, SIZE * BLOCK):
        raise ValueError(f"{png_path}: 期望 {SIZE * BLOCK}x{SIZE * BLOCK}，实际 {w}x{h}")

    def is_white(x, y):
        i = (y * w + x) * channels
        if channels >= 3:
            lum = (pixels[i] * 299 + pixels[i + 1] * 587 + pixels[i + 2] * 114) // 1000
        else:
            lum = pixels[i]
        return lum >= THRESHOLD

    # 先把整幅图二值化成一张位图，避免在下采样循环里反复解码像素
    src = [is_white(x, y) for y in range(h) for x in range(w)]

    bw, bh = w // SIZE, h // SIZE
    stride = (SIZE + 7) // 8
    rows = bytearray()
    for by in range(SIZE):
        acc = 0
        nbit = 0
        for bx in range(SIZE):
            hits = 0
            for y in range(by * bh, (by + 1) * bh):
                base = y * w + bx * bw
                for x in range(base, base + bw):
                    if src[x]:
                        hits += 1
            # 覆盖率阈值：块内白点比例达到就判白。取值偏低是为了保住细线 ——
            # 太空人的线条在 300x300 里只占几个像素宽，按 50% 判定会全部丢失。
            acc = (acc << 1) | (1 if hits * 100 >= bw * bh * COVERAGE_PERCENT else 0)
            nbit += 1
            if nbit == 8:
                rows.append(acc)
                acc, nbit = 0, 0
        if nbit:
            rows.append(acc << (8 - nbit))
    assert len(rows) == SIZE * stride, f"{png_path}: 位图长度不对"
    return bytes(rows)


def main():
    frames = sorted(
        f for f in os.listdir(SRC_DIR) if f.lower().endswith(".jpg")
    )
    if not frames:
        print(f"没有找到素材: {SRC_DIR}", file=sys.stderr)
        return 1

    tmp = "/tmp/dida_astro_png"
    os.makedirs(tmp, exist_ok=True)
    os.makedirs(os.path.dirname(OUT_C), exist_ok=True)

    parts = [
        "// 由 scripts/gen_astronaut.py 生成，请勿手工修改。",
        "//",
        "// 太空人动画：1 位掩码（A1），用 theme 的文字色 recolor 上色，",
        "// 因此一套素材同时适用于深色与浅色主题。",
        f"// 帧数 {len(frames)}，每帧 {SIZE}x{SIZE}，"
        f"每帧 {SIZE * ((SIZE + 7) // 8)} 字节。",
        "",
        '#include "lvgl.h"',
        "",
    ]
    refs = []
    total = 0
    for idx, name in enumerate(frames):
        png = os.path.join(tmp, f"{idx}.png")
        subprocess.run(
            ["sips", "-s", "format", "png",
             "-z", str(SIZE * BLOCK), str(SIZE * BLOCK),
             os.path.join(SRC_DIR, name), "--out", png],
            check=True, capture_output=True,
        )
        bits = to_a1_bits(png)
        total += len(bits)
        stride = (SIZE + 7) // 8
        parts.append(f"static const uint8_t astro_{idx}_map[] = {{")
        for off in range(0, len(bits), stride):
            row = ", ".join(f"0x{b:02X}" for b in bits[off : off + stride])
            parts.append(f"    {row},")
        parts.append("};")
        parts.append("")
        parts.append(f"static const lv_image_dsc_t astro_{idx} = {{")
        parts.append("    .header = {")
        parts.append("        .magic = LV_IMAGE_HEADER_MAGIC,")
        parts.append("        .cf = LV_COLOR_FORMAT_A1,")
        parts.append("        .flags = 0,")
        parts.append(f"        .w = {SIZE},")
        parts.append(f"        .h = {SIZE},")
        parts.append(f"        .stride = {stride},")
        parts.append("        .reserved_2 = 0,")
        parts.append("    },")
        parts.append(f"    .data_size = {len(bits)},")
        parts.append(f"    .data = astro_{idx}_map,")
        parts.append("    .reserved = nullptr,")
        parts.append("    .reserved_2 = nullptr,")
        parts.append("};")
        parts.append("")
        refs.append(f"    &astro_{idx},")

    parts.append("const lv_image_dsc_t* const dida_astro_frames[] = {")
    parts.extend(refs)
    parts.append("};")
    parts.append("")
    parts.append(f"const unsigned dida_astro_frame_count = {len(frames)};")
    parts.append("")

    io.open(OUT_C, "w", encoding="utf-8").write("\n".join(parts))
    print(f"已生成 {OUT_C}")
    print(f"  {len(frames)} 帧 x {SIZE}x{SIZE} = {total} 字节 ({total / 1024:.1f} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
