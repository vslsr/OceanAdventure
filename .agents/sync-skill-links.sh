#!/usr/bin/env bash
# 把 .agents/skills/ 下的每个 Skill 软链到各工具自己的 Skill 目录。
# 唯一真相是 .agents/skills/；.claude/skills/ 只放软链，不要往里放真实文件。
# 新增/删除 Skill 后跑一次：bash .agents/sync-skill-links.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/.agents/skills"

# 需要软链的工具目录（相对仓库根）。Codex 原生读 .agents/skills，不需要链。
TARGETS=(".claude/skills" ".trae/skills")

for target in "${TARGETS[@]}"; do
  dir="$ROOT/$target"
  mkdir -p "$dir"
  # 清掉旧软链（只删软链，真实目录保留并报错提醒）
  for entry in "$dir"/*; do
    [ -e "$entry" ] || [ -L "$entry" ] || continue
    if [ -L "$entry" ]; then
      rm "$entry"
    else
      echo "warn: $target/$(basename "$entry") 是真实目录，不是软链，已跳过（请迁到 .agents/skills/）" >&2
    fi
  done
  # 按 .agents/skills/ 重建
  depth_prefix="$(echo "$target" | awk -F/ '{for(i=1;i<=NF;i++) printf "../"}')"
  for skill in "$SRC"/*/; do
    name="$(basename "$skill")"
    [ -f "$skill/SKILL.md" ] || { echo "warn: $name 缺少 SKILL.md，跳过" >&2; continue; }
    ln -s "${depth_prefix}.agents/skills/$name" "$dir/$name"
  done
  echo "synced: $target ($(ls -1 "$dir" | wc -l) skills)"
done
