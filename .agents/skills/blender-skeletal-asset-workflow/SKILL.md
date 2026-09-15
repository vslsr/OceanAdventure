---
name: blender-skeletal-asset-workflow
description: 为 OceanAdventure 用 Blender Python 程序化生成带骨骼的资产并烘出动画 clip，导入 Unreal 成 SkeletalMesh + AnimSequence。用户要求给道具/武器/生物加骨骼、绑定蒙皮、做拉弓/开合/摆动之类的形变动画、烘 Action、导出动画 FBX，或问「骨骼绑定怎么做」「脚本里没有动画内容」「时间轴上没有关键帧」「动画导进 UE 不动/没形变」「弦/布/软体权重怎么给」时使用。纯静态模型与通用导出路径规范改用 `blender-asset-workflow`（本技能依赖它的工程根解析与目录规范）；`/Raft/Vehicles/<HullName>` 船体资产族改用 `raft-hull-asset-workflow`；Lyra AbilitySet/InputConfig/GameFeatureData 改用 `lyra-editor-asset-automation`。不负责 AnimBlueprint 连线、运行时玩法 C++ 或手工编辑 `.uasset`。
---

# Blender 骨骼资产与动画 clip 流水线

程序化生成「会动的东西」：网格 + 骨架 + 蒙皮 + 动画 clip，全部由一个 `bpy` 脚本产出，
再由 GameFeature 的编辑器脚本幂等导入。

先读 `python-script-governance`（强制门禁），再读 `blender-asset-workflow`
（工程根解析、`blender/script/python/`、`blender/models/` 目录规范）。本技能只补
骨骼、蒙皮、动画和它们在 UE 侧的落地，不重复那两条。

每条规则后标注了**验证状态**：`宿主已验证` 表示在真实 Blender/UE 里跑出过，
`未验证` 表示只经静态检查。不要把未验证的规则当既成事实转述给用户。

## 参考实现

- `blender/script/python/create_wood_bow.py` —— 骨架 + 蒙皮 + 两段 clip 的完整样板；
- `blender/script/python/create_round_body_character.py` —— 更早的刚性绑定样板；
- `Plugins/GameFeatures/OceanAdventure/Content/Python/CreateWoodBowAssets.py` —— UE 导入侧。

## 1. 骨架契约只能有一份

骨骼名跨两个解释器、两种语言，中间还隔着一个二进制 FBX。写两份必然漂移，而且**漂移是静默的**：
AnimBlueprint 驱动一根已不存在的骨头不会报错，只是不动。

做法：Blender 脚本里把骨骼名写成**纯字面量**元组（不要 `("root",) + DEFORM`，
`ast.literal_eval` 读不了表达式），UE 脚本用 `ast.literal_eval` 从那个 `.py` 里读回来逐一比对。
采样率、时长这类跨端常量同样只写一份。〔未验证〕

- `root` 是 0 号骨、**不形变**，UE 按它做 motion root；导入后断言 0 号骨确实是它。
- 形变骨必须各自有同名顶点组，缺一个就是一块不跟着动的几何。

## 2. 硬性规则（都踩过或都有明确机理）

### 2.1 pose 驱动必须换算到骨骼局部空间

`PoseBone.location` / `rotation_euler` 定义在**骨骼局部空间**，局部 Y 沿 head→tail，
X/Z 由 roll 决定。按世界轴写分量会静默推错方向。

```python
def to_bone_space(pose_bone, world_direction):
    return pose_bone.bone.matrix_local.to_3x3().inverted() @ mathutils.Vector(world_direction)
```

旋转同理：`Quaternion(to_bone_space(pb, (1,0,0)), angle)`，不要靠猜 roll 让局部 X 恰好等于世界 X。
〔宿主已验证：见失败档案 `PY-BLENDER-001`，把「沿 +Y 拉弦」写成局部 Z 偏移，位移落在与世界 Y
垂直的方向上，校验读出 0.0000m〕

### 2.2 叶子非形变骨活不到 FBX 里，挂点用 Socket

导出参数 `use_armature_deform_only=True` 只保留形变骨**和有形变子级的非形变骨**。
`root` 因为有形变子级得以保留；一根纯粹当挂点的叶子骨会被直接丢掉。

所以挂点（箭搭在哪、枪口在哪）**不要做成骨骼**，让 UE 导入脚本在 Skeleton 上建 Socket。
Socket 还能在绑定方式将来改变时活下来。〔未验证〕

### 2.3 蒙皮权重：能刚性就刚性，要插值才混合

- 整块跟着一根骨头走的部件（弓臂、刀身、炮管）→ 顶点组权重 1.0，一根骨头。
- 需要在两根骨头之间**弯成形状**的部件（弓弦、绳、布条）→ 沿主轴**线性**混合。

线性权重 + 纯平移，在线性混合蒙皮下恰好把一条直线映射成两段直线，也就是一个**精确的 V**。
换成常量权重或 smoothstep，形状会鼓成弧，再怎么调动画都掰不直。〔本地数值已验证；
Blender 侧权重归一化断言宿主已验证〕

### 2.4 网格 FBX 必须在任何动画数据存在之前导出

UE 从网格 FBX 读绑定姿势。导出时骨架上挂着 Action 或摆着姿势，资产里那件东西就永远
半开着。顺序固定为：建网格 → 校验 → **导出网格 FBX** → 烘 clip → 导出 clip FBX。〔未验证〕

### 2.5 网格 FBX 与动画 FBX 必须共用一份导出设置

骨骼集合或坐标轴在两份之间不一致，UE 判「skeleton does not match」，而那时没人记得
改过其中一份。抽成一个 dict 展开到两处：

```python
UE_FBX_COMMON = dict(
    use_selection=True, global_scale=1.0, apply_unit_scale=True,
    apply_scale_options="FBX_SCALE_UNITS", use_space_transform=True,
    bake_space_transform=False, axis_forward="-Y", axis_up="Z",
    add_leaf_bones=False, primary_bone_axis="Y", secondary_bone_axis="X",
    use_armature_deform_only=True, armature_nodetype="NULL",
    path_mode="AUTO", embed_textures=False,
)
```

〔宿主已验证：这组参数产出的 `blender/models/SK_RoundBodyCharacter.fbx` 在仓库里，
UE 可用；本次新脚本尚未跑到导出这一步〕

### 2.6 Action Slot 可能让 Action 空着

Blender 4.4+ 的 Action 通过 slot 路由通道。新建的空 Action 没绑上 slot 时，
`keyframe_insert` 照样返回成功而 Action 是空的 —— 导出的就是一段什么都不做的 clip。
烘完立刻断言 `action.fcurves` 非空。〔未验证，机理来自 4.4 slot 机制〕

### 2.7 不要用 pose 模式算子

`bpy.ops.pose.*` / `mode_set` 依赖上下文，在 `blender --background` 下是静默空操作的常见来源。
直接写 `pose_bone.location` / `.rotation_quaternion` 再 `view_layer.update()`。〔未验证〕

## 3. 动画 clip 怎么做

**先判断这段动画跟的是什么**，两类驱动方式完全不同，选错了后面全是补丁：

| | 跟玩法量（蓄力、开合度、血量） | 跟时钟（回弹、抖动、一次性反馈） |
|---|---|---|
| 叫法 | 姿势斜坡 | 表演 |
| 关键帧 | **两帧，线性** | 逐帧烘 |
| UE 侧 | 按 `量 * length` **显式采样**，不播放 | 播放一次 |
| 缓动放哪 | 玩法量的曲线里 | clip 里 |

姿势斜坡烘成非线性关键帧是最常见的错误：缓动会被施加两次。

手感常量（拉多开、回弹多久、过冲多少）只写一份，clip 由它们**生成**，不要手摆关键帧 ——
否则改手感要同时改常量和一堆键，漏一处没人会发现。

细节（曲线怎么移植成纯函数、采样率怎么定、UE 导入的重采样陷阱）见
[references/animation-clips.md](references/animation-clips.md)。

## 4. 验证阶梯

按能跑的层级从下往上，**不要跳级**：

1. **纯函数层**：把手感曲线、几何解算写成不认识 `bpy` 的纯函数，用 stub 在普通 CPython 里跑断言。
   这一层在没有 Blender 的机器上也能给出真实的红/绿：

   ```python
   import sys, types, importlib.util
   bpy = types.ModuleType("bpy")
   bpy.data = types.SimpleNamespace(filepath="")
   bpy.types = types.SimpleNamespace(Mesh=object, Armature=object)
   sys.modules["bpy"] = bpy
   sys.modules["mathutils"] = types.ModuleType("mathutils")
   spec = importlib.util.spec_from_file_location("m", "<script>.py")
   m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
   ```

2. **脚本内静态断言**：骨骼集合、父子关系、顶点组、**权重归一化**、材质槽数。
3. **脚本内形变读回**：把 rig 摆到极限姿势，`evaluated_get(depsgraph).to_mesh()` 读回顶点，
   确认该动的动了、不该动的没动。**第 2 层全绿的 rig 照样可能一动不动**，这一层才拦得住。
4. **clip 播放读回**：把 clip 播回骨架读姿势。只断言「有关键帧」挡不住被重采样压平的曲线。
5. **宿主运行**：`blender --background --python <script>`。

失败信息要带够区分度。位移校验只报单轴分量时，「完全没动」和「动错方向」长得一模一样 ——
`PY-BLENDER-001` 正是后者被报成了前者，白白怀疑了一遍蒙皮权重。同时报三维位移模长。

## 5. Unreal 导入衔接

沿用 `blender-asset-workflow` 的幂等导入规范，另加三条：

- **Skeleton 必须复用**。导入器拿不到 `skeleton` 就新建一个，重跑留下 `..._Skeleton_1`，
  而 AnimBP、蒙太奇、socket 全都还指着原来那个。从**已存在的 mesh 上读回** skeleton
  喂回导入选项（不要按名字猜），导入后断言 mesh 的 skeleton 包路径没变。〔未验证〕
- **动画导入要显式指定采样率**，见 references。
- **Socket 数组按名字（字符串）过滤后整体重建，并断言最终长度**。对 UObject 用
  `in` / `not in` 会退化成身份比较，过滤永不命中，重建变成只追加的累加器
  —— 机理与失败档案 `PY-LYRA-004` 同源。存在性检查对重复条目同样返回真，挡不住。〔未验证〕

首次导入必须在**完整编辑器**里做：UE 5.7 Interchange 完成路径会碰 Slate，
PythonScript commandlet 没有 SlateApplication（失败档案 `PY-UE-006`）。

## 6. 相邻技能边界

- 纯静态模型、导出路径与 `blender/models` 规范：`blender-asset-workflow`（本技能依赖它）。
- `/Raft/Vehicles/<HullName>` 船体资产族：`raft-hull-asset-workflow`。
- Lyra AbilitySet / InputConfig / GameFeatureData：`lyra-editor-asset-automation`。
- Python 门禁与失败留档：`python-script-governance`（错误记档案，不要抄进本技能）。
- Blender 5.x 引擎通用 API（modifier、bmesh、算子）：`blender-modeling-modifiers`、
  `blender-python-scripting`。
- AnimBlueprint 连线、GameplayAbility、运行时 C++：对应 UE5 玩法技能，本技能不覆盖。
