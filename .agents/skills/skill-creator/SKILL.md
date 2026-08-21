---
name: skill-creator
description: 在本仓库创建或普通维护 `.agents/skills` 下的 Codex 技能，设计准确触发边界、渐进披露结构和必要资源。适用于尚无真实实现轨迹的新技能或一般技能编辑；已完成模块的经验复盘改用 skill-consolidate，只做评测改用 skill-grader。
license: Apache-2.0
---

# Skill Creator

创建让 Codex 改变关键决策、减少重复调查的项目技能。默认目标目录是仓库 `.agents/skills/<skill-name>/`。

## 原则

- 假设 Codex 已具备通用能力，只写项目特有、非显而易见或容易出错的知识。
- 保留用户意图和授权边界。技能不能把普通任务扩张为提交、推送、删除或外部操作。
- 风险越高，步骤和停止条件越具体；开放任务保留合理选择空间。
- `description` 负责低成本发现，必须写清能力、触发语句、适用范围和必要的负场景。
- `SKILL.md` 放共同流程；只在确有条件分支时增加 `references/`，只在确定性重复逻辑值得自动化时增加 `scripts/`。

## 结构

```text
skill-name/
|-- SKILL.md
|-- scripts/       # 可选
|-- references/    # 可选
|-- assets/        # 可选，仅输出资源
`-- agents/        # 可选 UI 元数据
```

每个技能必须有 `SKILL.md`，frontmatter 至少包含：

```yaml
---
name: lowercase-hyphen-name
description: 说明做什么、何时使用，以及最容易误触发的相邻负场景。
---
```

名称只用小写字母、数字和连字符，最长 64 字符，目录名与 `name` 一致。

## 工作流

1. 收集真实请求、适用范围、相邻技能和可观察成功判据。缺失信息只有在会改变边界时才询问。
2. 先检查 `.agents/skills/*/SKILL.md` frontmatter；避免重复能力和同名技能。
3. 选择最小结构。简单技能只写 `SKILL.md`，不创建占位目录、README 或示例。
4. 写明期望结果、非显而易见约束、权限边界、失败征兆和验证方法。
5. 用 `scripts/quick_validate.py <skill-dir>` 做结构验证，再用 `skill-grader` 检查触发与行为。
6. 检查引用存在、脚本已运行、差异只包含授权范围。未经明确要求不提交或推送。

## 边界

- 已实现并验证的工作流复盘、归属和多技能边界维护：改用 `skill-consolidate`。
- 仅评价现有技能、输出 PASS/REVIEW/FAIL：改用 `skill-grader`。
- 不修改 `.agents/plugins` 中的插件技能，除非用户明确把插件本身置于范围内。
- 不把一次失败样例变成无条件全局规则；需要证据或稳定的项目约束。
