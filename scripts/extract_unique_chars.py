#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从项目目录下的文本源文件中收集「出现过一次的字符」集合（去重）。
默认包含：汉字、英文字母、数字、标点及各类符号；可选排除空白。
用法:
  python scripts/extract_unique_chars.py
  可选：在脚本同目录放置 extensions.txt，追加扩展名（与内置列表合并），无需额外参数。
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# 内置默认扩展名；若存在「脚本同目录/extensions.txt」则与其中的扩展名合并
DEFAULT_TEXT_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hpp",
    ".hh",
    ".inl",
    ".txt",
    ".md",
    ".xml",
    ".json",
    ".cmake",
    ".bat",
    ".ps1",
    ".sh",
    ".py",
    ".rc",
    ".properties",
}

# 忽略的目录名
SKIP_DIRS = {
    ".git",
    "build",
    "dist",
    "node_modules",
    ".vs",
    "__pycache__",
}

MAX_FILE_BYTES = 4 * 1024 * 1024


def _extensions_txt_path() -> Path:
    return Path(__file__).resolve().parent / "extensions.txt"


def parse_extensions_file(path: Path) -> set[str]:
    """从 UTF-8 文本读取扩展名：空格 / 换行 / 制表分隔；可写 .lang 或 lang；忽略逗号。"""
    raw = path.read_text(encoding="utf-8", errors="replace")
    out: set[str] = set()
    for line in raw.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        for tok in line.split():
            t = tok.strip().strip(",").lower()
            if not t or t.startswith("#"):
                continue
            if not t.startswith("."):
                t = "." + t
            out.add(t)
    return out


def load_extensions_for_scan() -> set[str]:
    """内置扩展名 ∪ 脚本同目录 extensions.txt（若存在）。"""
    base = set(DEFAULT_TEXT_EXTENSIONS)
    path = _extensions_txt_path()
    if not path.is_file():
        return base
    try:
        extra = parse_extensions_file(path)
    except OSError as e:
        print(f"警告：无法读取 {path}，仅使用内置扩展名。（{e}）", file=sys.stderr)
        return base
    return base | extra


def iter_text_files(root: Path, extensions: set[str]):
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        parts = set(p.parts)
        if parts & SKIP_DIRS:
            continue
        if p.suffix.lower() not in extensions:
            continue
        try:
            if p.stat().st_size > MAX_FILE_BYTES:
                continue
        except OSError:
            continue
        yield p


def collect_chars(root: Path, skip_ws: bool, extensions: set[str]) -> set[str]:
    chars: set[str] = set()
    for path in iter_text_files(root, extensions):
        try:
            data = path.read_bytes()
        except OSError:
            continue
        text = data.decode("utf-8", errors="replace")
        for ch in text:
            if skip_ws and ch.isspace():
                continue
            chars.add(ch)
    return chars


def _sort_key_codepoint(ch: str) -> int:
    return ord(ch)


def _sort_key_grouped(ch: str) -> tuple[int, int]:
    """
    分段排序（便于阅读，接近常见「符号→英文数字→其它标点→汉字」的习惯）：
      0 - ASCII 可打印字符 U+0021～U+007E（以 ! 开头、"~" 结尾这一段）
      1 - 空格 U+0020（单独一段；若未收集空格则不会出现）
      2 - 其余码位小于 U+4E00 的字符（含 … ≈ 、。「」等全角标点）
      3 - CJK 统一表意 U+4E00～U+9FFF
      4 - 其它（扩展汉字区、谚文等）
    控制字符（Tab/换行等）排在最后一段，避免插在中间。
    """
    cp = ord(ch)
    if cp < 0x20 or cp == 0x7F:
        return (5, cp)
    if ch.isspace() and cp != 0x20:
        return (5, cp)
    if 0x21 <= cp <= 0x7E:
        return (0, cp)
    if cp == 0x20:
        return (1, cp)
    if cp < 0x4E00:
        return (2, cp)
    if 0x4E00 <= cp <= 0x9FFF:
        return (3, cp)
    return (4, cp)


def sort_chars(chars: set[str], mode: str) -> str:
    if mode == "codepoint":
        ordered = sorted(chars, key=_sort_key_codepoint)
    elif mode == "grouped":
        ordered = sorted(chars, key=lambda c: _sort_key_grouped(c))
    else:
        raise ValueError(f"unknown sort mode: {mode}")
    return "".join(ordered)


def main() -> int:
    ap = argparse.ArgumentParser(description="提取项目中文字符并去重")
    ap.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="项目根目录（默认为本仓库根）",
    )
    ap.add_argument(
        "--out",
        type=Path,
        default=None,
        help="输出文件路径（默认打印到 stdout）",
    )
    ap.add_argument(
        "--skip-whitespace",
        action="store_true",
        help="不收集空白字符（空格/换行/制表等）",
    )
    ap.add_argument(
        "--sort",
        choices=("grouped", "codepoint"),
        default="grouped",
        help=(
            "排序方式：grouped=分段排序（默认，先 !\"#$…英文数字符号，再其它标点，再汉字）；"
            "codepoint=纯 Unicode 码位从小到大"
        ),
    )
    args = ap.parse_args()

    root = args.root.resolve()
    if not root.is_dir():
        print(f"错误：不是目录 {root}", file=sys.stderr)
        return 1

    ext_set = load_extensions_for_scan()

    chars = collect_chars(root, skip_ws=args.skip_whitespace, extensions=ext_set)
    ordered = sort_chars(chars, args.sort)

    if args.out:
        args.out.write_text(ordered, encoding="utf-8")
        print(f"已写入 {args.out} ，共 {len(chars)} 个不同字符。", file=sys.stderr)
    else:
        sys.stdout.reconfigure(encoding="utf-8") if hasattr(sys.stdout, "reconfigure") else None
        print(ordered)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
