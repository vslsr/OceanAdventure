#!/usr/bin/env python3
"""Fail the build when a committed file hardcodes an absolute path.

AGENTS.md bans absolute paths outright, but until now nothing enforced it, and that is
exactly why they kept coming back: a doc copied a command from another doc, the source was
later fixed, the copy was not. One scan makes the ban checkable instead of aspirational.

What counts as a violation
    A Windows drive path (``C:\\...`` / ``e:/...``) or a POSIX home path (``/home/<user>``,
    ``/Users/<user>``) in any committed text file.

What does not
    Registry roots (``HKLM:``/``HKCU:``), URLs, and the two shapes listed in ALLOW below --
    the ban's own counter-examples, and Epic's fixed install locations used to *discover*
    where the engine is. Verbatim error text in the Python failure ledger is evidence and is
    exempt as a whole block; see FENCED_EVIDENCE.

Usage
    python Tools/check_absolute_paths.py           # scan the repo, exit 1 on any violation
    python Tools/check_absolute_paths.py --list    # print what is allowlisted and why
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

SUCCESS_MARKER = "ABSOLUTE_PATH_CHECK_OK"

SKIP_DIRS = {
    ".git",
    "Intermediate",
    "Binaries",
    "Saved",
    "DerivedDataCache",
    "node_modules",
    "__pycache__",
}

# Third-party material this repository does not own; AGENTS.md exempts both by name.
SKIP_PREFIXES = (
    ".agents/vendor/",
    ".agents/plugins/",
)

SKIP_SUFFIXES = {
    ".uasset", ".umap", ".fbx", ".obj", ".glb", ".blend", ".png", ".jpg", ".jpeg",
    ".tga", ".wav", ".mp3", ".ttf", ".otf", ".pdf", ".zip", ".dll", ".exe", ".pdb",
    ".lib", ".bin", ".csv", ".ico", ".ush", ".usf",
}

DRIVE_PATH = re.compile(r"(?<![A-Za-z0-9_])[A-Za-z]:[\\/]")
HOME_PATH = re.compile(r"/(?:home|Users)/[A-Za-z0-9_.-]+")
REGISTRY_ROOT = re.compile(r"HK(?:LM|CU|CR|U|CC):")
URL = re.compile(r"[a-z][a-z0-9+.-]*://")

# (path relative to repo root, literal that may appear on the line, why it is allowed).
# Keep this list short. A new entry means someone decided a machine path earns its place --
# that should be an argued exception, not a habit.
ALLOW: list[tuple[str, str, str]] = [
    ("AGENTS.md", "D:\\UEPrj\\", "禁令自己举的反例"),
    ("AGENTS.md", "C:\\EpicWkspc\\", "禁令自己举的反例"),
    ("AGENTS.md", "E:\\", "禁令自己举的反例"),
    ("AGENTS.md", "D:\\build\\++UE5\\Sync\\", "说明失败档案豁免范围时引用的引擎源码路径"),
    ("README.md", "D:\\UEPrj\\", "禁令自己举的反例"),
    ("README.md", "C:\\EpicWkspc\\", "禁令自己举的反例"),
    ("README.md", "E:\\", "禁令自己举的反例"),
    ("README.md", "C:\\ProgramData\\Epic\\", "Epic Launcher 的固定登记位置，用来探测引擎装在哪"),
    ("doc/line-style-helper/readme.md", "C:\\ProgramData\\Epic\\",
     "同上：探测引擎位置，不是假设工程位置"),
    ("Plugins/NwiroIntegrationKit/Source/NwiroIntegrationKit/Private/NwiroIKPathSandbox.cpp",
     "C:\\Windows\\", "路径穿越沙箱的反例注释，说明它拒绝什么"),
    ("Plugins/NwiroIntegrationKit/Source/NwiroIntegrationKit/Private/NwiroIKPathSandbox.h",
     r'"C:\\Windows\\..."', "同上"),
]

# This file necessarily contains every pattern it hunts for -- the allowlist literals and the
# examples in the docstring. Scanning itself would mean maintaining an allowlist of the
# allowlist, so it is skipped outright, the way a linter skips its own rule table.
SELF = Path(__file__).resolve()

# Files whose fenced code blocks hold verbatim evidence that must stay byte-for-byte.
# Prose in these files is still scanned -- an entry line someone will copy is not evidence.
FENCED_EVIDENCE = {
    ".agents/skills/python-script-governance/references/error-ledger.md",
}


def is_allowed(rel_path: str, line: str) -> str | None:
    for allow_path, literal, reason in ALLOW:
        if rel_path == allow_path and literal in line:
            return reason
    return None


def scan_file(path: Path) -> list[tuple[int, str]]:
    rel = path.relative_to(REPO_ROOT).as_posix()
    try:
        text = path.read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        return []

    skip_fenced = rel in FENCED_EVIDENCE
    in_fence = False
    findings: list[tuple[int, str]] = []

    for number, line in enumerate(text.splitlines(), start=1):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if skip_fenced and in_fence:
            continue

        stripped = URL.sub("", line)
        stripped = REGISTRY_ROOT.sub("", stripped)
        if not (DRIVE_PATH.search(stripped) or HOME_PATH.search(stripped)):
            continue
        if is_allowed(rel, line):
            continue
        findings.append((number, line.strip()))

    return findings


def iter_files() -> list[Path]:
    files: list[Path] = []
    for path in REPO_ROOT.rglob("*"):
        if not path.is_file() or path.is_symlink():
            continue
        rel = path.relative_to(REPO_ROOT).as_posix()
        if any(part in SKIP_DIRS for part in path.relative_to(REPO_ROOT).parts):
            continue
        if rel.startswith(SKIP_PREFIXES):
            continue
        if path.suffix.lower() in SKIP_SUFFIXES:
            continue
        if path.resolve() == SELF:
            continue
        files.append(path)
    return files


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="Print the allowlist and exit.")
    args = parser.parse_args(argv)

    if args.list:
        print(f"允许的例外（{len(ALLOW)} 条）：")
        for allow_path, literal, reason in ALLOW:
            print(f"  {allow_path}: {literal!r}  —— {reason}")
        print(f"整块豁免 fenced 代码块（原始报错是证据）：{', '.join(sorted(FENCED_EVIDENCE))}")
        return 0

    violations = 0
    for path in sorted(iter_files()):
        rel = path.relative_to(REPO_ROOT).as_posix()
        for number, line in scan_file(path):
            print(f"{rel}:{number}: {line}")
            violations += 1

    if violations:
        print(f"\n发现 {violations} 处写死的绝对路径。", file=sys.stderr)
        print(
            "按 AGENTS.md 的四种写法改：仓库内文件从脚本自身位置推导；UE 资产用 /Plugin/... "
            "虚拟路径；编辑器 Python 用模块名 import；仓库外只走环境变量且不给默认值。\n"
            "确属例外的，在 Tools/check_absolute_paths.py 的 ALLOW 里加一条并写明理由。",
            file=sys.stderr,
        )
        return 1

    print(SUCCESS_MARKER)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
