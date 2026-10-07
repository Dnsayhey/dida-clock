#!/usr/bin/env python3
"""生成 LVGL 界面字库。

为什么不用 LVGL 内置的 lv_font_montserrat_*
-------------------------------------------
* 内置字体是 157 字形的拉丁扩展集，本界面只用得到其中一部分；而 70px 的
  大号数字若用全字符集要 90 KB，自定义纯数字只要 9 KB。
* 中文必须自定义子集 —— 全量 CJK 有 1 MB 量级，4 MB flash 放不下。

字符集是怎么定的
----------------
* **地名**取和风天气城市列表的**省 ∪ 市 ∪ 区县**合并去重（1297 字）：
  adm1 65 + adm2 428 + name 1284 → 去重后 1297。
  界面显示的是 name（搜索命中的区/县/县级市，如搜"萧山"就显示"萧山"），
  所以区县字**必须**在字库里；省市一并留下，是为了 lookup 失败、回退显示
  用户手输地名（可能是"浙江"）时不缺字。这份字集固化在
  components/assets/charset/city.txt，生成时不需要联网。
* **界面文案**从源码的字符串字面量里扫出来（scripts/extract_cjk.py）。
  这样改文案后重新生成一次就够，不必手工维护字符清单。
* **运行期数据的词汇**单列在 charset/runtime.txt。这一份**必须手工维护**，
  因为那些字永远不会出现在源码里 —— 风向"东风"、空气质量"轻度污染"、
  天气现象"雷阵雨伴有冰雹"全是接口返回的。漏了它们，屏幕上那些位置就是
  空白（实机上就漏过"风"字，整条信息显示成"东 4级"）。
* **标题**是少数几个固定词，单独列在这里 —— 它们要用 28px，与正文不同字号。

用法：python3 scripts/gen_fonts.py
依赖：node/npm（npx 自动拉取 lv_font_conv）、macOS 或 Linux 的字体文件
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import extract_cjk  # noqa: E402  (需要在 sys.path 设置之后导入)

ASSETS = os.path.join(ROOT, "components", "assets")
FONT_DIR = os.path.join(ASSETS, "font")
CITY_CHARSET = os.path.join(ASSETS, "charset", "city.txt")
RUNTIME_CHARSET = os.path.join(ASSETS, "charset", "runtime.txt")

# 源字体。SourceHanSansSC 随 LVGL 仓库分发；Montserrat 需自备（OFL 许可）。
SHS = os.path.join(
    ROOT, "managed_components", "lvgl__lvgl", "scripts", "built_in_font",
    "SourceHanSansSC-Normal.otf",
)
MON = os.environ.get("MONTSERRAT", "/tmp/fonts/Montserrat-Medium.ttf")

# 天气大字只需 9 类短名 —— 官方 text 长短差异极大（"晴"1 字 ↔
# "雷阵雨伴有冰雹"7 字），固定字号放不下，所以大字用分类、小字给完整名。
WEATHER_SHORT = "晴云阴雨雷雹雪雾沙"

# 页面标题。它们用 28px，与正文分属不同字号，所以必须单独列。
# 新增页面标题时把它加到这里。
TITLE_CHARS = "配网日预报设置恢复出厂"


def read_charset_file(path, what):
    if not os.path.exists(path):
        raise SystemExit(f"缺少{what}: {path}")
    text = io_read(path)
    # 字集文件里除了汉字还会写成多行/带空白，全部按"字符"处理即可
    return {c for c in text if c.strip()}


def io_read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def source_charset():
    """扫描源码字符串字面量里的全部汉字。"""
    chars = set()
    for path in extract_cjk.iter_sources():
        text = io_read(path)
        for lit in extract_cjk.string_literals(text):
            # 用 needs_glyph（所有非 ASCII）而不是 is_cjk：
            # 必须与 check_fonts.py 的判定一致，否则生成的字库永远比校验
            # 认为需要的少几个字符 —— 全角标点（），这类就会一直报缺字。
            chars |= {c for c in lit if extract_cjk.needs_glyph(c)}
    return chars


def run_conv(args):
    cmd = ["npx", "--yes", "lv_font_conv"] + args
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout, file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        raise SystemExit(f"lv_font_conv 失败: {' '.join(args[:6])}...")


def fix_include(path):
    """把 lv_font_conv 生成的 include 改成 IDF 能解析的形式。

    生成的是：
        #ifdef LV_LVGL_H_INCLUDE_SIMPLE
        #include "lvgl.h"
        #else
        #include "lvgl/lvgl.h"
        #endif
    但 LV_LVGL_H_INCLUDE_SIMPLE 只有 LVGL 的桌面版 CMake 才会定义，ESP-IDF 的
    LVGL 组件把头文件放在 lvgl.h，于是 else 分支找不到文件。

    放在脚本里而不是手工改 —— 否则下次重新生成就丢了。
    """
    text = io_read(path)
    old = (
        "#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n"
        '#include "lvgl.h"\n'
        "#else\n"
        '#include "lvgl/lvgl.h"\n'
        "#endif"
    )
    if old not in text:
        raise SystemExit(f"{path}: 未找到预期的 include 块，lv_font_conv 版本变了？")
    with open(path, "w", encoding="utf-8") as f:
        f.write(text.replace(old, '#include "lvgl.h"', 1))


def make_tabular(path):
    """把 0~9 改成**等宽数字**（tabular figures），并关掉字距调整。

    为什么必须这样
    --------------
    时分是「HH:MM」，宽度随数字浮动：'1' 只有 26px，而 '0'/'4' 有 47px，
    于是**同一个位置**在不同时刻宽度能差 84px。而秒的 x 是写死的，两者
    坐标系对不上 —— 实测全部 1440 个时刻里：

        34% 的间距为负（时间直接压到秒上，最严重 -22px）
        最宽的能到 62px（看起来"太空"）

    只统一 `adv_w` **不够**：LVGL 实际算的是 `adv_w + kv`，而这个字库有 33
    个非零字距对（数字之间最大 -53/16 px）。必须**同时**关掉 kern，否则
    等宽会被字距重新破坏。数字时钟本来就该用等宽数字，去掉字距是对的。

    ':' 不参与等宽 —— 它本来就该窄。
    """
    text = open(path, encoding="utf-8").read()

    # 逐条取出 glyph_dsc 的 adv_w（第 0 条是保留位，对应 gid=0）
    dsc_at = text.index("glyph_dsc[]")
    dsc_end = text.index("};", dsc_at)
    block = text[dsc_at:dsc_end]
    advs = [int(v) for v in re.findall(r"\.adv_w = (\d+)", block)]

    # 用 cmap 把字符映射成 gid
    gid_of = {}
    for m in re.finditer(
            r"\.range_start = (\d+),\s*\.range_length = (\d+),\s*"
            r"\.glyph_id_start = (\d+)", text):
        start, length, gid0 = (int(g) for g in m.groups())
        for code in range(start, start + length):
            gid_of.setdefault(chr(code), gid0 + (code - start))

    digits = [gid_of[c] for c in "0123456789" if c in gid_of]
    if not digits:
        raise SystemExit(f"{path}: 找不到数字字形，等宽处理失败")
    widest = max(advs[g] for g in digits)

    idx = -1

    def repl(m):
        nonlocal idx
        idx += 1
        return f".adv_w = {widest}" if idx in digits else m.group(0)

    text = text[:dsc_at] + re.sub(r"\.adv_w = \d+", repl, block) + text[dsc_end:]

    # **把每个数字的墨迹在格子里居中。**
    #
    # 只统一 adv_w 是不够的：各个数字的墨迹宽度差很多（'1' 只有 17px，'4' 有
    # 41px），而 Montserrat 自带左边距是为**比例排版**设计的，强制等宽后
    # '1' 会紧贴格子左边、右边空出 26px —— 看上去就是"1 和后面那个数字之间
    # 一大片空白"。等宽数字本来就该把墨迹居中，这一步不能省。
    dsc_at = text.index("glyph_dsc[]")
    dsc_end = text.index("};", dsc_at)
    block = text[dsc_at:dsc_end]
    rows = re.findall(
        r"\{(\.bitmap_index = \d+, \.adv_w = \d+, \.box_w = \d+, \.box_h = \d+, "
        r"\.ofs_x = )(-?\d+)(.*?)\}",
        block)
    if len(rows) <= max(digits):
        raise SystemExit(f"{path}: glyph_dsc 条目比预期少，居中处理失败")
    for gid in digits:
        head, _, tail = rows[gid]
        box_w = int(re.search(r"\.box_w = (\d+)", head).group(1))
        adv = int(re.search(r"\.adv_w = (\d+)", head).group(1))
        centred = round(((adv / 16.0) - box_w) / 2.0)
        block = block.replace(
            f"{head}{rows[gid][1]}{tail}",
            f"{head}{centred}{tail}", 1)
    text = text[:dsc_at] + block + text[dsc_end:]

    # 关掉字距调整，否则它会重新破坏等宽。
    #
    # 必须**整段删掉**那三张表，不能只把描述符指成 NULL —— 表会变成
    # "defined but not used"，在 -Werror=unused-const-variable 下直接编译失败。
    # 删掉也顺带省了 flash。
    text = text.replace(".kern_dsc = &kern_classes,", ".kern_dsc = NULL,", 1)
    text = text.replace(".kern_classes = 1,", ".kern_classes = 0,", 1)
    text = re.sub(
        r"/\*Map glyph_ids to kern left classes\*/.*?"
        r"static const lv_font_fmt_txt_kern_classes_t kern_classes =\s*\{.*?\};\n",
        "", text, flags=re.S)
    # 校验只能查**表本体**的名字。不能查 "kern_classes =" —— 描述符里的
    # `.kern_classes = 0,` 就含这个子串，会把"删除成功"误判成"没删干净"，
    # 于是 SystemExit 拦住写入，字库永远停在半成品状态。
    if "kern_left_class_mapping" in text:
        raise SystemExit(f"{path}: 字距表没有完全删除，检查 gen_fonts.py")

    open(path, "w", encoding="utf-8").write(text)
    return widest


def gen(name, size, fonts):
    """fonts 是 (路径, 该字体负责的字符) 的列表 —— 每个 --font 必须紧跟
    自己的 --symbols，不能共用。"""
    out = os.path.join(FONT_DIR, f"{name}.c")
    args = ["--bpp", "4", "--size", str(size), "--no-compress"]
    for path, chars in fonts:
        args += ["--font", path, "--symbols", chars]
    args += ["--format", "lvgl", "--lv-font-name", name, "-o", out]
    run_conv(args)
    fix_include(out)
    return out


def main():
    for path in (SHS, MON):
        if not os.path.exists(path):
            raise SystemExit(
                f"缺少字体文件: {path}\n"
                f"（Montserrat 可用 MONTSERRAT=/path/to/Montserrat-Medium.ttf 指定）"
            )

    os.makedirs(FONT_DIR, exist_ok=True)
    city = read_charset_file(CITY_CHARSET, "城市字集")
    runtime = read_charset_file(RUNTIME_CHARSET, "运行期字集")
    source = source_charset()
    body_cn = "".join(sorted(city | runtime | source))
    title = "".join(sorted(set(TITLE_CHARS)))
    latin = "".join(chr(c) for c in range(0x20, 0x7F))

    print(f"  城市 {len(city)} 字 + 运行期 {len(runtime)} 字 + 源码 "
          f"{len(source)} 字 → 正文去重 {len(body_cn)} 字")
    print(f"  标题 {len(title)} 字，天气大字 {len(WEATHER_SHORT)} 字")

    # 时间：64px 纯数字，**等宽**
    #
    # 为什么不是 70px：时分最宽的时刻等宽后是 4x47 + 16 = 204px，加上 22px
    # 的秒（30px）就超出 240px 屏宽。64px 下时分 187px + 秒 30px + 边距
    # 8+8 = 233px，留 7px 余量。
    make_tabular(gen("dida_num_64", 64, [(MON, "0123456789:")]))
    # 温度：26px，℃ 取自中文字体（Montserrat 没有这个码位）
    gen("dida_num_26", 26, [(MON, "0123456789-"), (SHS, "℃")])
    # 秒：22px，同样等宽（这样秒自身的右边缘也不抖）
    make_tabular(gen("dida_num_22", 22, [(MON, "0123456789")]))
    # 页面标题：28px
    gen("dida_cn_28", 28, [(MON, latin), (SHS, title)])
    # 天气大字：44px，9 类短名
    gen("dida_cn_44", 44, [(SHS, WEATHER_SHORT)])
    # 正文：16px，中文 + 拉丁混排
    gen("dida_cn_16", 16, [(MON, latin), (SHS, body_cn + "℃·")])

    # 导出覆盖清单。校验脚本拿它比对源码用字，缺字就报错 ——
    # 这样"改了文案忘记重新生成字库"不会拖到烧板子才发现。
    covered = set(body_cn) | set(title) | set(WEATHER_SHORT) | set("℃·")
    with open(os.path.join(FONT_DIR, "COVERAGE.txt"), "w", encoding="utf-8") as f:
        f.write("".join(sorted(covered)) + "\n")

    total = 0
    for name in ("dida_num_64", "dida_num_26", "dida_num_22", "dida_cn_28",
                 "dida_cn_44", "dida_cn_16"):
        path = os.path.join(FONT_DIR, f"{name}.c")
        size = os.path.getsize(path)
        total += size
        print(f"  {name:<14} {size / 1024:>8.1f} KB（源码文本）")
    print(f"  字库已生成到 components/assets/font/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
