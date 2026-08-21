#!/usr/bin/env python3
"""Minimal structural validator for a Codex skill."""

import re
import sys
from pathlib import Path


ALLOWED_FRONTMATTER = {"name", "description", "license", "allowed-tools", "metadata"}


def parse_frontmatter(text):
    """Parse the top-level scalar fields needed by this validator without PyYAML."""
    values = {}
    top_level_keys = set()
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line[:1].isspace():
            continue
        match = re.match(r"^([A-Za-z0-9_-]+):(?:[ \t]*(.*))?$", line)
        if not match:
            return None, None, f"Unsupported frontmatter line: {line}"
        key, raw_value = match.groups()
        top_level_keys.add(key)
        value = (raw_value or "").strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key] = value
    return values, top_level_keys, None


def validate(skill_dir):
    skill_dir = Path(skill_dir)
    skill_file = skill_dir / "SKILL.md"
    if not skill_file.is_file():
        return False, "SKILL.md not found"

    content = skill_file.read_text(encoding="utf-8")
    match = re.match(r"^---\r?\n(.*?)\r?\n---", content, re.DOTALL)
    if not match:
        return False, "Invalid YAML frontmatter"

    frontmatter, top_level_keys, parse_error = parse_frontmatter(match.group(1))
    if parse_error:
        return False, parse_error
    unexpected = top_level_keys - ALLOWED_FRONTMATTER
    if unexpected:
        return False, f"Unexpected frontmatter keys: {', '.join(sorted(unexpected))}"

    name = frontmatter.get("name")
    description = frontmatter.get("description")
    if not isinstance(name, str) or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", name):
        return False, "name must be lowercase hyphen-case"
    if len(name) > 64 or skill_dir.name != name:
        return False, "name must be <=64 characters and match the directory"
    if not isinstance(description, str) or not description.strip():
        return False, "description is required"
    if len(description) > 1024:
        return False, "description must be <=1024 characters"
    if "[TODO:" in content:
        return False, "unfinished TODO placeholder found"

    markdown_links = re.findall(r"\[[^\]]+\]\(([^)]+)\)", content[match.end():])
    for link in markdown_links:
        if "://" in link or link.startswith("#"):
            continue
        target = (skill_dir / link.split("#", 1)[0]).resolve()
        if not target.exists():
            return False, f"missing local reference: {link}"

    return True, "Skill is valid"


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: quick_validate.py <skill-directory>")
        raise SystemExit(2)
    valid, message = validate(sys.argv[1])
    print(message)
    raise SystemExit(0 if valid else 1)
