---
name: raft-hull-asset-workflow
description: 为 OceanAdventure/Raft 新建或重建独立船体载具资产族，包括符合 200×200×150 cm 建筑模块规范的 Blender 模型、独立 UE 资产构建 Python、SM/DA/BP 绑定和可建造性配置。用户提到“新船体/救生筏/船体载具”、`DA_Raft_*`、`BP_Raft_*`、`/Raft/Vehicles` 或要求每种船体单独生成时使用；普通道具/武器 Blender 模型改用 blender-asset-workflow，运行时 RaftActor、浮力、复制或 GAS 实现不使用本技能。
---

# Raft 船体资产工作流

为每一种船体建立可以独立重建、独立导入、独立验证的资产族。不要让一种船体的生成依赖舰炮、舵台、其它船体或完整 Naval 流水线。

## 船体资产契约

开始前确定稳定的 `HullName`，然后同时创建以下所有权边界：

```text
Blender 建模脚本
└─ blender/script/python/SM_<HullName>.py

Blender 原始输出
├─ blender/models/SM_<HullName>.blend
└─ blender/models/SM_<HullName>.fbx

UE 运行时资产目录
└─ /Raft/Vehicles/<HullName>/
   ├─ SM_<HullName>
   ├─ DA_Raft_<HullName>
   ├─ BP_Raft_<HullName>
   └─ 此船体专属材质

UE 独立构建入口
└─ Plugins/GameFeatures/Raft/Content/Python/CreateRaft<HullName>Assets.py
```

- 每种船体必须使用自己的 `/Raft/Vehicles/<HullName>` 文件夹；不要把新船体塞进 `/Raft/Vehicles/Raft` 或另一个船体目录。
- 每种船体必须有自己的 UE 资产构建 Python 入口。综合脚本可以调用它或复用公共函数，但独立入口必须能单独生成该船体的 `SM/DA/BP`。
- 独立入口的前置条件只能包含该船体真正需要的资产。缺少舰炮、舵台或其它建造件不得阻断单独创建船体。
- 不直接编辑、复制或在文件系统中移动 `.uasset`。路径迁移在 UE Content Browser 或幂等编辑器脚本中完成，确认引用后再 Fix Up Redirectors。

## Blender 模型与建筑模块规范

先应用 `blender-asset-workflow` 的通用目录、工程根探测、幂等集合清理和 UE FBX 导出规则；本技能只增加以下船体约束。

船体美术可以比主木筏小，但必须共享 Raft 的模块原点和包络约定：

- 一个完整建筑/船体模块是 `2.0 × 2.0 × 1.5 m`。
- 原点在模块碰撞盒中心；可见网格必须留在 `±1.0 m X/Y、±0.75 m Z` 内，并留出小幅美术边距。
- 导出 `UCX_SM_<HullName>_00`，尺寸严格为 `2.0 × 2.0 × 1.5 m`。
- 小型救生筏通过缩小可见轮廓体现差异，不改变模块碰撞和原点约定。浮力采样点可按可见船体收紧。
- 合并可见部件为一个 `SM_<HullName>`，保留稳定材质槽和自定义元数据，再单独导出网格与 UCX。
- 在脚本内验证可见包络、UCX 尺寸和最终输出路径；失败时停止，不把文件写进其它工程。

## 独立 UE 资产构建脚本

脚本必须幂等，并按以下顺序执行：

1. 在修改资产前检查 `blender/models/SM_<HullName>.fbx` 存在；缺失时明确提示先运行对应 Blender 脚本。
2. 用 `AssetImportTask` 导入到 `/Raft/Vehicles/<HullName>`，启用 `replace_existing`，合并网格，导入材质但不导入纹理。
3. 在同一目录 get-or-create `DA_Raft_<HullName>`，类型为 `URaftDefinition`。
4. 设置 `visual_mesh`、`deck_box_extent=(100,100,75) cm`、零 `visual_mesh_offset`、与可见船体匹配的四个 `pontoon_offsets` 及浮力调参。
5. 明确配置可建造性：
   - 可建造船体引用获准的 append-only `BuildPieceCatalog`；
   - 不可建造船体显式设置 `build_piece_catalog=None`。
6. get-or-create `BP_Raft_<HullName>`，父类为 `ARaftActor`；设置 CDO 的 `raft_definition`，并把根 `USceneComponent` 的 `Mobility` 明确设为 `Movable`。编译后重新获取 GeneratedClass/CDO，再次修复并读回 Mobility；根组件为 Static 时 UE 会直接拒绝浮力或驾驶产生的 Actor 移动。
7. 读回验证 DA 保留独立网格、BP 保留 DA、根组件为 Movable、可建造性与设计一致；任一项失败即抛错。
8. 若运行时设置引用该船体，同步更新为 `/Raft/Vehicles/<HullName>/BP_Raft_<HullName>.BP_Raft_<HullName>_C`。

公共实现可以放在库脚本中，由独立入口以非 `__main__` 名称加载并只调用该船体函数。不要让独立入口执行综合脚本的 `main()`。

## 常见失败与二元判据

- `Could not locate OceanAdventure project root`：按 `blender-asset-workflow` 修复通用根探测并重载 Blender 文本块；船体侧判据仍是工程根、同级 Lyra 和伪路径三种入口都解析到 OceanAdventure。
- `Missing /NavalCore/Naval/BP_Naval_Cannon`：运行了完整 Naval 构建入口。不要降低完整流水线门禁；改运行该船体的独立构建脚本。判据是无舰炮资产时独立船体脚本仍能成功。
- 资产落到 `/Raft/Vehicles/Raft`：船体根常量仍指向旧目录。用一个 `HULL_ROOT` 派生 SM/DA/BP 三条路径，并同步运行时配置。
- FBX 缺失时脚本已经改了其它资产：前置检查顺序错误。必须在任何资产写入前失败。
- BP/DA 写入后读回不一致：编译可能替换 GeneratedClass；保存后重新获取 CDO 并断言引用。
- `DefaultSceneRoot has to be 'Movable'`、`root component has mobility 'Static'` 或 UE 拒绝 `SetActorLocation`：生成的 BP 根组件 Mobility 未持久化。设置 CDO 根组件为 `ComponentMobility.MOVABLE`，编译后重新获取 CDO 再修复和断言；只修改可见 StaticMesh 的 Mobility 不足以移动 Actor。
- 旧目录残留同名 DA/BP：不要从文件系统删除二进制；先验证新路径引用，再在 Content Browser 删除旧资产并修复重定向器。

LifeRaft 的已验证实现、具体锚点和原始故障轨迹见 [LifeRaft 参考轨迹](references/life-raft-reference.md)。

## 验证门禁

交付前全部满足：

1. Blender 与 UE Python 均通过 AST 解析。
2. 根目录解析测试覆盖 OceanAdventure、同级 LyraStarterGame 和 `\SM_<HullName>.py` 文本块伪路径。
3. Blender 重跑后只重建自有集合，并在 `blender/models` 生成非空 `.blend/.fbx`。
4. 独立 UE 构建入口可在无无关 Naval 资产时运行两次，第二次不产生重复资产。
5. `/Raft/Vehicles/<HullName>` 同时包含正确的 SM/DA/BP；其它船体目录未被写入。
6. DA 的模块范围、网格、浮力点和 BuildPieceCatalog 读回正确；BP CDO 指向该 DA，且 Actor 根组件 Mobility 读回为 Movable。
7. 运行时配置（若有）指向新 Blueprint 类路径。
8. 在 PIE 验证外观、站立碰撞和设计的可建造/不可建造行为。若改动了复制代码，再按工程规范使用 Dedicated Server + 至少两个客户端；纯资产重建不宣称已验证复制逻辑。

## 相邻技能边界

- 通用 Blender 道具、武器、场景模型、FBX 路径或单纯根目录故障：使用 `blender-asset-workflow`。
- AbilitySet、InputConfig 或 GameFeatureData Python：使用 `lyra-editor-asset-automation`。
- `ARaftActor`、浮力、移动、复制、BuildStructure C++ 行为：使用对应 UE gameplay/architecture/debug 技能，不在本技能内顺带修改。
- 是否应使用 GameplayAbility、由谁承载输入和生命周期：使用 `gameplay-ability-selection`。
