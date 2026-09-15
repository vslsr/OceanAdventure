#!/usr/bin/env bash
# 维护 Skill 的统一入口 .agents/skills/，并软链到各工具自己的 Skill 目录。
#
#   真实 Skill      → .agents/skills/<name>/              （唯一真相，手写、进版本库）
#   插件自带 Skill  → .agents/skills/<name> 软链到插件目录（插件 sync 拥有，勿直接改）
#   各工具目录      → .claude/skills/、.trae/skills/ 全是软链
#
# 新增/删除 Skill、或插件同步过之后跑一次：bash .agents/sync-skill-links.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/.agents/skills"

# 需要软链的工具目录（相对仓库根）。Codex 原生读 .agents/skills，不需要链。
TARGETS=(".claude/skills" ".trae/skills")

mkdir -p "$SRC"

# ---- 1. 插件自带的 Skill 挂进 .agents/skills/ --------------------------------
# 只挂软链，不搬文件：插件的 skills/ 目录是 sync 脚本拥有的，它会按 catalog
# 删掉目录里任何对不上的条目。真把 Skill 搬进去，下次同步就被 rm -rf。
# 先清掉已失效的软链（插件那边删了 Skill 就会变成断链）。
for entry in "$SRC"/*; do
  [ -L "$entry" ] || continue
  [ -f "$entry/SKILL.md" ] || { echo "prune: $(basename "$entry") 的插件来源已消失，移除断链" >&2; rm "$entry"; }
done

for plugin_skill in "$ROOT"/.agents/plugins/*/skills/*/; do
  [ -f "$plugin_skill/SKILL.md" ] || continue
  name="$(basename "$plugin_skill")"
  link="$SRC/$name"
  rel="../plugins/${plugin_skill#"$ROOT"/.agents/plugins/}"
  rel="${rel%/}"
  if [ -L "$link" ]; then
    [ "$(readlink "$link")" = "$rel" ] || ln -sfn "$rel" "$link"
  elif [ -e "$link" ]; then
    echo "warn: .agents/skills/$name 已是真实 Skill，与插件同名，跳过插件那份" >&2
  else
    ln -s "$rel" "$link"
  fi
done

# ---- 2. 按 .agents/skills/ 重建各工具目录 -----------------------------------
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
  depth_prefix="$(echo "$target" | awk -F/ '{for(i=1;i<=NF;i++) printf "../"}')"
  for skill in "$SRC"/*/; do
    name="$(basename "$skill")"
    [ -f "$skill/SKILL.md" ] || { echo "warn: $name 缺少 SKILL.md，跳过" >&2; continue; }
    ln -s "${depth_prefix}.agents/skills/$name" "$dir/$name"
  done
  echo "synced: $target ($(ls -1 "$dir" | wc -l) skills)"
done
