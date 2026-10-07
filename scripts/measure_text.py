#!/usr/bin/env python3
"""按 LVGL 的实际算法计算字符串的渲染宽度。

为什么需要这个工具
------------------
界面上不少槽位是**固定宽度**的（未来天气页的四列、AQI 徽章等）。宽度定小了，
文字会换行 —— 而"换行"在实机上表现为"某个字掉到下一行"，看起来像随机故障，
极难定位。本项目的 7 日预报页就踩过：按字形的 adv_w 简单累加算 "10-10" 是
39.4px，放进 42px 的槽却换行了。

真实算法（managed_components/lvgl__lvgl/src/font/lv_font_fmt_txt.c）比"累加
adv_w"多两件事，任一件漏掉都会少算：

  1. **字距调整（kerning）**参与其中：
         kv = (kvalue * kern_scale) >> 4
         adv_w = (gdsc->adv_w + kv + 8) >> 4        <- 每个字形各自四舍五入到整像素

     注意取整是**逐字形**做的，不是最后统一取整 —— 5 个字形的误差可以累积到 2~3px。

  2. 相邻字形要一起查（最后一个字形用 gid=0 作后继，不参与 kerning）。

用法：
    python3 scripts/measure_text.py dida_cn_16 "10-10" "中到大雪" "16/26℃"
    python3 scripts/measure_text.py --all          # 把所有页面的固定槽位核一遍
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(ROOT, "components", "assets", "font")


class LvglFont:
    """从 lv_font_conv 生成的 .c 里读回度量，复现 LVGL 的宽度算法。"""

    def __init__(self, path):
        s = io.open(path, encoding="utf-8").read()
        self.src = s
        body = s[s.find("glyph_dsc[]") : s.find("};", s.find("glyph_dsc[]"))]
        self.adv = [int(m) for m in re.findall(r"\.adv_w = (\d+)", body)]
        self.line_height = int(re.search(r"\.line_height = (\d+)", s).group(1))

        # cmap 段：ASCII 段（range_start=32, glyph_id_start=1）与稀疏段
        self.ascii_start = None
        self.ascii_len = 0
        self.sparse_start = None
        self.sparse_ids = []
        for m in re.finditer(
            r"\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),"
            r"\s*\.unicode_list = (\w+),.*?\.list_length = (\d+)",
            s, re.S,
        ):
            start, length, gid_start, ulist, list_len = m.groups()
            start, gid_start, list_len = int(start), int(gid_start), int(list_len)
            if ulist == "NULL":
                self.ascii_start, self.ascii_len = start, gid_start
                self.ascii_gid_start = gid_start
            else:
                body = re.search(ulist + r"\[\]\s*=\s*\{(.*?)\};", s, re.S).group(1)
                offs = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", body)]
                self.sparse_start = start
                self.sparse_gid_start = gid_start
                self.sparse_ids = offs

        # kern classes
        #
        # 判据不能只看有没有 "kern_classes" 这个字符串：字库描述符里
        # `.kern_classes = 0`（没有 kern 表）也会命中，随后去读
        # kern_left_class_mapping 就崩了 —— 纯汉字子集字库（如 dida_cn_44，
        # 只有 12 个汉字、无 ASCII）正是这种情况。
        # 必须确认三张表本体都在。
        # 判据有两层，缺一不可：
        #   1. 三张 kern 表本体在（生成物里它们总是被写出来）；
        #   2. **描述符确实引用了它们**（`.kern_dsc = &...`）。
        #
        # 只看第 1 层会误判：等宽字库（gen_fonts.py 的 make_tabular）会把
        # `.kern_dsc` 置为 NULL 来关掉字距，但三张表仍留在源码里 ——
        # 此时字距**不生效**，工具却会按"有字距"去算，宽度就错了。
        self.kern = None
        if (all(t in s for t in ("kern_left_class_mapping",
                                 "kern_right_class_mapping",
                                 "kern_class_values"))
                and re.search(r"\.kern_dsc\s*=\s*&", s)):
            self.left_map = self._ints("kern_left_class_mapping")
            self.right_map = self._ints("kern_right_class_mapping")
            self.class_values = self._ints("kern_class_values", signed=True)
            self.left_cnt = int(re.search(r"\.left_class_cnt\s*=\s*(\d+)", s).group(1))
            self.right_cnt = int(re.search(r"\.right_class_cnt\s*=\s*(\d+)", s).group(1))
            self.kern = True

    def _ints(self, name, signed=False):
        body = re.search(name + r"\[\]\s*=\s*\{(.*?)\};", self.src, re.S).group(1)
        pat = r"-?\d+" if signed else r"\d+"
        return [int(x) for x in re.findall(pat, body)]

    def gid(self, ch):
        """字符 -> glyph id（1 起）。找不到返回 0。"""
        c = ord(ch)
        if self.ascii_start is not None and self.ascii_start <= c < self.ascii_start + 0x5F:
            return c - self.ascii_start + 1
        if self.sparse_ids:
            off = c - self.sparse_start
            if off in self.sparse_ids:
                return self.sparse_gid_start + self.sparse_ids.index(off)
        return 0

    def kern_value(self, gid_left, gid_right):
        if not self.kern or gid_left == 0 or gid_right == 0:
            return 0
        if gid_left >= len(self.left_map) or gid_right >= len(self.right_map):
            return 0
        lc = self.left_map[gid_left]
        rc = self.right_map[gid_right]
        if lc == 0 or rc == 0:
            return 0
        return self.class_values[(lc - 1) * self.right_cnt + (rc - 1)]

    def width(self, text):
        """字符串的渲染宽度（像素），算法与 LVGL 一致。"""
        total = 0
        for i, ch in enumerate(text):
            g = self.gid(ch)
            if g == 0:
                raise KeyError(f"字库里没有 {ch!r}")
            nxt = self.gid(text[i + 1]) if i + 1 < len(text) else 0
            kv = (self.kern_value(g, nxt) * 16) >> 4
            total += (self.adv[g] + kv + 8) >> 4
        return total


def weather_categories():
    """从 weather_category.cpp 里读出九个分类名。

    **样本必须从源码来，不能手写。** 手写的样本表有个致命弱点：代码改了它不知道。
    这个槽位就栽过 —— 样本里写着单字，而代码里"多云/冰雹/沙尘"是两个字的，
    检查一路绿灯，实机上大字压住温度 30px。
    """
    src = io.open(os.path.join(ROOT, "components", "weather",
                               "weather_category.cpp"), encoding="utf-8").read()
    return re.findall(r'constexpr char k\w+\[\] = "([^"]+)";', src)


# 页面上的固定宽度槽位：(页面, 字体, 槽宽, 内容说明, 内容)
SLOTS = [
    # 日期列：**必须按最宽的日期测**，不能只看当天。
    # "1" 只有 6px，而 "0"/"8"/"9" 有 10~11px —— 实际踩的坑就是拿
    # "10-10"(40px) 当样本、给了 42px 槽，而当天显示的是 "10-09"(44px)，
    # 于是换行。最宽的是 "08-08" 这类两个月都由宽数字组成的日期。
    ("future_weather 日期", "dida_cn_16", 50,
     ["10-09", "10-10", "08-08", "09-28", "12-31", "01-01"]),
    ("future_weather 星期", "dida_cn_16", 34, ["周一", "周六", "周日", "今天"]),
    ("future_weather 天气", "dida_cn_16", 66,
     ["晴", "多云", "雷阵雨", "中到大雪", "雷阵雨伴有冰雹"]),
    ("future_weather 温度", "dida_cn_16", 66,
     ["16/26℃", "20/30℃", "08/09℃", "-8/-3℃"]),
    # 大字天气：它**没有独立槽位**，而是"必须在大字起点到温度起点之间放得下"。
    # 也就是说可用宽度 = kTemperatureX - kWeatherX = 72 - 14 = 58px。
    #
    # 这一项曾经漏登记，代价是：多云/冰雹/沙尘 三个分类是两个字（88px），
    # 实机上直接压住温度 30px。**分类名必须是单字**，理由见 weather_category.cpp。
    # 样本直接来自 weather_category.cpp —— 手写会随代码漂移（见该函数注释）
    ("real_time 大字天气", "dida_cn_44", 58, weather_categories()),
    ("real_time 城市", "dida_cn_16", 48, ["杭州", "乌鲁木齐", "呼和浩特"]),
    ("real_time 天气全名", "dida_cn_16", 56, ["晴", "中到大雨", "雷阵雨伴有冰雹"]),
]


def main():
    if "--all" in sys.argv:
        fonts = {}
        bad = 0
        print(f"  {'槽位':<26}{'槽宽':>5}{'最宽内容':>10}{'需要':>7}{'余量':>8}")
        print("  " + "-" * 58)
        for name, font_name, slot, samples in SLOTS:
            if font_name not in fonts:
                fonts[font_name] = LvglFont(os.path.join(FONT_DIR, font_name + ".c"))
            f = fonts[font_name]
            worst, worst_txt = 0, ""
            missing = []
            for t in samples:
                try:
                    w = f.width(t)
                except KeyError as e:
                    # **字形缺失必须报错，不能当 0 或 -1 跳过。**
                    # 曾经这里写的是 `w = -1`，于是"槽位的字体里根本没有这个字"
                    # 被静默忽略、结论仍然是"放得下" —— 而那才是最坏的情况：
                    # 屏幕上会直接缺字。这个槽位就栽过（分类名改成两个字后，
                    # 44px 字库没有"多"，检查却一路绿灯）。
                    missing.append(f"{t} {e}")
                    continue
                if w > worst:
                    worst, worst_txt = w, t
            if missing:
                bad += 1
                print(f"  {name:<26}{slot:>5}{'—':>10}{'—':>6}     缺字形  <-- "
                      f"{'；'.join(missing)}")
                continue
            margin = slot - worst
            mark = "" if margin >= 0 else "  <-- 会换行/截断"
            if margin < 0:
                bad += 1
            print(f"  {name:<26}{slot:>5}{worst_txt:>10}{worst:>6}px{margin:>+7}px{mark}")
        print()
        print(f"  {'发现 ' + str(bad) + ' 处放不下' if bad else '全部放得下'}")
        return 1 if bad else 0

    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    f = LvglFont(os.path.join(FONT_DIR, sys.argv[1] + ".c"))
    for text in sys.argv[2:]:
        try:
            print(f"  {text!r}: {f.width(text)}px  （行高 {f.line_height}px）")
        except KeyError as e:
            print(f"  {text!r}: {e}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
