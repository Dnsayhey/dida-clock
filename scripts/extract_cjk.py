#!/usr/bin/env python3
"""从源码里提取所有中文字符。

用途：字库是编译期固定的，而文案是源码里的字符串。如果两边的字符集不同步，
缺的字在屏幕上就是空白/方框，而且**只有烧到板子上才会发现**。这个脚本让
"源码实际用到哪些字"变成可计算的，从而可以自动生成字库、自动校验覆盖。

只统计**会画到 LCD 上**的字符串。因此排除两类：
  * SKIP_FILES 里的文件（强制门户返回的 HTML，由手机浏览器渲染）；
  * 日志与断言调用里的字符串（走串口，或只在编译失败时出现）。

用法：
    python3 scripts/extract_cjk.py            # 打印字符集（一行）
    python3 scripts/extract_cjk.py --list     # 每个文件各用了哪些字
    python3 scripts/extract_cjk.py --selftest # 自检"该排的排了、该留的留了"
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAN_DIRS = ["components", "main"]
SKIP_PARTS = ("assets/font", "assets/charset", "assets/image", "managed_components")

# 只认代码文件；字库 .c 是生成物，扫它会把整个字库当"用到"的字符。
EXTS = (".c", ".cpp", ".h")

# 这些文件里的字符串**不会显示在设备屏幕上**，所以不需要设备字库的字形。
#
# 强制门户返回的是 HTML，由**手机浏览器**用自己的字体渲染 —— 往设备字库里
# 塞这些字（"保存并连接""省份 / 地区"…）纯属浪费，一页表单就是几十个字形。
# 排除掉才能让"字库覆盖源码用字"这条检查保持它原本的含义：
# **凡是会画到 LCD 上的字，字库里必须有**。
SKIP_FILES = (
    "components/portal/portal_page.cpp",
    "components/portal/portal_http.cpp",
)

# 这些调用里的字符串**不会画到屏幕上**，同样不需要字形：
#   * ESP_LOGx —— 走串口控制台，LCD 上看不到；
#   * static_assert / assert —— 消息只在断言失败时出现，不进正常显示路径。
#
# 不排除它们的代价很具体：往日志里加一句中文，字库覆盖检查就会失败，于是不得
# 不重新生成字库 —— 一次上千行生成产物的改动，只为几个永远不上屏的字形。
# 判据与 SKIP_FILES 完全一致：**会不会画到 LCD 上**。
LOG_CALL_RE = re.compile(r"\b(?:ESP_LOG[EWIDV]|static_assert|assert)\s*\(")


def excluded_spans(text):
    """返回"不需要字库"的字符串区间 [(start, end), ...]。

    区间 = 从调用名的左括号，到与之配对的右括号（含）。必须按括号配对扫描，
    而不是找最近的 ')' —— 参数里可能再套括号；扫描时还要跳过字符串/字符
    字面量与注释，否则 `ESP_LOGI(t, "(")` 这种会把配对算错，进而把后面的
    界面文案一起吞掉。
    """
    spans = []
    n = len(text)
    for match in LOG_CALL_RE.finditer(text):
        i = match.end() - 1  # 指向 '('
        depth = 0
        while i < n:
            c = text[i]
            if c == "/" and i + 1 < n and text[i + 1] == "/":
                end = text.find("\n", i)
                i = n if end < 0 else end
                continue
            if c == "/" and i + 1 < n and text[i + 1] == "*":
                end = text.find("*/", i + 2)
                i = n if end < 0 else end + 2
                continue
            if c in "\"'":
                quote = c
                i += 1
                while i < n and text[i] != quote:
                    i += 2 if text[i] == "\\" else 1
                i += 1
                continue
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    spans.append((match.start(), i + 1))
                    break
            i += 1
    return spans


def is_in_spans(pos, spans):
    """pos 是否落在某个区间里。spans 按起点升序（finditer 保证）。"""
    for start, end in spans:
        if pos < start:
            return False
        if pos < end:
            return True
    return False


def is_cjk(c):
    return "\u4e00" <= c <= "\u9fff"


def needs_glyph(c):
    """这个字符是否需要字库里有对应字形。

    **不限汉字** —— 非 ASCII 的符号同样要占字形。实机上就漏过一个：启动页
    用 "▸"(U+25B8) 标出当前进度，而字库里没有它，屏幕上就是一个方块。
    只查汉字的话这类问题查不出来。
    """
    return ord(c) > 0x7F


def string_literals(text, skip_spans=None):
    """逐个吐出 C/C++ 字符串字面量的内容。

    只取字面量、不取注释，是因为本项目注释里中文很多（约 640 个不同的字），
    把它们算进字库会让 16px 字库白涨 80 KB 左右。

    必须自己扫而不是正则匹配引号：源码里有大量含 `//` 的 URL 字符串
    （"https://..."），用正则会被引号配对搞乱；这里按状态机走，
    正确处理字符串内的转义与字符字面量。

    skip_spans 内（日志/断言调用的实参）的字面量不会被吐出；默认按
    excluded_spans() 计算，所以调用方不必关心。
    """
    if skip_spans is None:
        skip_spans = excluded_spans(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        # 行注释：跳到行尾
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            i = text.find("\n", i)
            if i < 0:
                return
            continue
        # 块注释：跳到 */
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue
        # 字符字面量：跳过（内部可能是转义）
        if c == "'":
            i += 1
            while i < n and text[i] != "'":
                i += 2 if text[i] == "\\" else 1
            i += 1
            continue
        # 字符串字面量：产出内容
        if c == '"':
            literal_start = i
            i += 1
            out = []
            while i < n and text[i] != '"':
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i + 1])
                    i += 2
                    continue
                out.append(text[i])
                i += 1
            i += 1
            if not is_in_spans(literal_start, skip_spans):
                yield "".join(out)
            continue
        i += 1


def iter_sources():
    for base in SCAN_DIRS:
        for dirpath, dirnames, filenames in os.walk(os.path.join(ROOT, base)):
            rel = os.path.relpath(dirpath, ROOT)
            if any(part in rel for part in SKIP_PARTS):
                continue
            for name in filenames:
                if not name.endswith(EXTS):
                    continue
                path = os.path.join(dirpath, name)
                if os.path.relpath(path, ROOT) in SKIP_FILES:
                    continue
                yield path


# 自检用的样本。要点不是"日志被排除了"，而是**排除不能越界**：
# 日志实参里有括号、有转义引号时，配对不能算错，否则会把后面的界面文案一起
# 吞掉 —— 那才是这条规则真正危险的失效方式（漏字只有烧到板子上才看得见）。
SELFTEST_SOURCE = """
// 注释里的"上屏文案"不算
const char* a = "界面文案甲";
ESP_LOGI(kTag, "日志文案 %s", value);
ESP_LOGW(kTag, "带括号 ( 和 ) 还有转义引号 \\" 的日志");
static_assert(x > 0, "断言文案");
const char* b = "界面文案乙";
assert(y && "断言文案二");
const char* c = "界面文案丙";
"""

SELFTEST_EXPECTED = {"界面文案甲", "界面文案乙", "界面文案丙"}


def selftest():
    """自检：该排的排掉、该留的留下。返回 0 表示通过。

    为什么需要它：这条规则让"字库覆盖"检查**变得更宽松**。如果配对扫描写错，
    检查会静默地少要求一些字形，而症状（屏幕上缺字）只有实机才看得出来。
    所以必须有一组固定样本证明"排除只发生在该排除的地方"。
    """
    got = set(string_literals(SELFTEST_SOURCE))
    problems = []
    for missing in sorted(SELFTEST_EXPECTED - got):
        problems.append(f"应当提取却漏掉：{missing!r}")
    for leaked in sorted(got - SELFTEST_EXPECTED):
        problems.append(f"不该提取却出现：{leaked!r}")

    if problems:
        print("extract_cjk 自检失败：")
        for p in problems:
            print(f"  {p}")
        return 1
    print("extract_cjk 自检通过（日志/断言已排除，界面文案仍提取）")
    return 0


def main():
    if "--selftest" in sys.argv:
        return selftest()

    per_file = {}
    for path in iter_sources():
        text = io.open(path, encoding="utf-8", errors="ignore").read()
        found = {c for lit in string_literals(text) for c in lit
                 if needs_glyph(c)}
        if found:
            per_file[os.path.relpath(path, ROOT)] = found

    all_chars = set()
    for chars in per_file.values():
        all_chars |= chars

    if "--list" in sys.argv:
        for path in sorted(per_file):
            print(f"{path}: {''.join(sorted(per_file[path]))}")
        return 0

    sys.stdout.write("".join(sorted(all_chars)))
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
