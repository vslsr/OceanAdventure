# .agents/vendor

上游 Skill 包里**不属于 Skill 本体**的资料：课程讲义、许可证、包自带的 README 与截图、
包自带的校验脚本。放这里只是留档参考，**不参与任何工具的 Skill 加载**。

Skill 本体一律在 `.agents/skills/<skill-name>/SKILL.md`，见 AGENTS.md「Skill 目录（统一入口）」。

| 目录 | 来源 | 现在还有什么 |
|---|---|---|
| `UE5-Skills/` | UnrealEngine5-Skills 技能包 | README、LICENSE、截图，以及 `scripts/validate_skills.py`（默认校验 `.agents/skills/` 下的 `ue5-*`） |
| `XG-Lyra-Course-Skill/` | Lyra 精讲课程包 | 只留课程 README（出处与作者）；`knowledge/`、`docs/` 讲义原文已从工作区删除，需要时从 git 历史取回 |

两个包里的 Skill 本体（11 个 `ue5-*` 和 `xg-lyra-course`）已迁到 `.agents/skills/`，这里不再留副本。
