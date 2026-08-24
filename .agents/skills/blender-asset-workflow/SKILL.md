---
name: blender-asset-workflow
description: 为 OceanAdventure 维护 Blender 原始模型与 Unreal 导入流水线。用户要求创建或修复 Blender 模型脚本、FBX/OBJ/GLB 导出路径、`blender/models` 资源规范，或遇到 Blender 文本块找不到 OceanAdventure 工程根目录时使用；不负责直接编辑 UE `.uasset` 二进制，不替代 `lyra-editor-asset-automation` 的 AbilitySet/InputConfig/GameFeatureData 配置，也不负责运行时 Actor/GameplayAbility 实现。
---

# Blender 资源流水线

## 目录归属

- Blender Python 脚本统一放在项目根目录 `blender/script/python/`。
- 新建或导出的 Blender 原始工程与模型文件（`.blend`、`.fbx`、`.obj`、`.glb` 等）统一放在项目根目录 `blender/models/`；历史文件可按需迁移。
- GameFeature 专属运行时资源仍必须放在 `Plugins/GameFeatures/<Feature>/Content/`。原始 FBX 不放入插件的 `ArtSource/`；Unreal 编辑器脚本从 `blender/models/` 导入到拥有该玩法的 Content 路径。
- 仓库中面向旧 LyraStarterGame 的历史脚本或示例可能仍出现 `blender/fbx`；只有修改或重新接入这类流水线时才迁移它们，新资产不得沿用旧路径。

## 导出脚本规则

1. 导出前只删除脚本自己创建的命名集合，不清理或覆盖无关 Blender 对象。
2. 输出路径使用项目根目录解析结果：`<project>/blender/models/<AssetName>.fbx`，不要把当前工作目录或 `.blend` 文本块的伪路径当成项目根。
3. Blender 文本块可能显示为 `some.blend/script.py`，因此工程根目录探测应优先识别同时满足以下条件的目录：
   - 目录内存在任意 `.uproject`；
   - 存在 `Plugins/GameFeatures/OceanAdventure/`。
4. 从 LyraStarterGame 或其他同级工程启动 Blender 时，向已发现的 `.uproject` 根目录的同级目录查找 OceanAdventure；也支持显式环境变量 `OCEAN_ADVENTURE_PROJECT_ROOT` 覆盖。
5. 找不到工程根目录时停止并报告期望路径，不要把 FBX 导出到 LyraStarterGame、插件目录或磁盘临时目录。
6. 导出的 FBX 应使用 UE 兼容单位与坐标设置，并在导出后检查文件存在且大小合理。

## Unreal 导入衔接

对应的 Unreal Python 脚本应：

- 从 `Path(unreal.Paths.project_dir()) / "blender" / "models"` 读取原始文件；
- 用 `AssetImportTask` 幂等导入到所属 GameFeature 的 Content 目录；
- 创建或更新 Blueprint/数据资产时使用稳定资产路径，重跑不追加重复 Action；
- 导入失败时报告缺失原始文件和先运行的 Blender 脚本，不静默回退到错误工程。

## 验证

- 用 Python AST 解析脚本；
- 在项目根目录和同级 `LyraStarterGame` 目录分别测试根目录解析结果；
- 检查 FBX 输出路径为 `<project>/blender/models/`，UE 资产路径仍位于对应 GameFeature 的 Content；
- 未经用户明确要求，不启动或连接 Blender；交付时说明未执行的 Blender/UE 生成步骤。

## 相邻技能边界

- 需要配置 Lyra AbilitySet、InputConfig 或 GameFeatureData：改用 `lyra-editor-asset-automation`。
- 需要选择 GameplayAbility 与世界 Actor 的职责：改用 `gameplay-ability-selection`。
- 需要实现 UE Actor、Projectile 或 GameplayAbility C++：改用对应 UE5 gameplay 技能。
