#!/usr/bin/env python3
"""校验字库是否覆盖源码里用到的全部汉字。

为什么需要这个检查
------------------
字库是**编译期固定**的，而文案是源码里的字符串。改了文案却忘了重新生成字库，
缺的字在屏幕上就是空白/方框 —— 而且**构建、单测全都会通过**，只有烧到板子上
用眼睛看才会发现。这类"代码写对了但资源没跟上"的问题，靠记性守不住。

检查范围是**源码里所有非 ASCII 字符**，不限汉字 —— 实机上漏过一个 U+25B8
的箭头，屏幕上就是方块。

本脚本拿 components/assets/font/COVERAGE.txt（生成时导出）与源码实际用字比对，
缺任何一个就报错并列出"哪个字、哪个文件在用"。

用法：python3 scripts/check_fonts.py
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import extract_cjk  # noqa: E402

COVERAGE = os.path.join(ROOT, "components", "assets", "font", "COVERAGE.txt")


def main():
    if not os.path.exists(COVERAGE):
        print(f"找不到 {COVERAGE}，请先运行 scripts/gen_fonts.py", file=sys.stderr)
        return 1

    with open(COVERAGE, encoding="utf-8") as f:
        covered = set(f.read().strip())

    missing = {}
    for path in extract_cjk.iter_sources():
        with open(path, encoding="utf-8", errors="ignore") as f:
            text = f.read()
        used = set()
        for lit in extract_cjk.string_literals(text):
            used |= {c for c in lit if extract_cjk.needs_glyph(c)}
        gap = used - covered
        if gap:
            missing[os.path.relpath(path, ROOT)] = gap

    if not missing:
        print("字库覆盖检查通过")
        return 0

    print("字库缺字（改完文案需要重新运行 scripts/gen_fonts.py）：")
    for path in sorted(missing):
        print(f"  {path}: {''.join(sorted(missing[path]))}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
