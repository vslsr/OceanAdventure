# 海军可装配模块与建筑模块美术规范

OceanAdventure 的模块统一分为两类，尺寸不能混用：

1. **可装配模块**：炮、船舵等附着在建筑或船体上的功能模块，使用 `160 × 160 × 140 cm` 包络。
2. **建筑模块**：墙体、地基、建筑核心、船体等拼接为整体建筑的模块，使用 `200 × 200 × 150 cm` 包络。

## 1. 可装配模块：160 × 160 × 140 cm

炮的 `ANavalHeavyWeaponActor::WeaponCollision` 是可装配模块的运行时基准：

```cpp
WeaponCollision->SetBoxExtent(FVector(80.0, 80.0, 70.0));
WeaponCollision->SetRelativeLocation(FVector(0.0, 0.0, 70.0));
```

本地碰撞盒根部位于 `Z=0`，中心位于 `Z=70 cm`，安全边为每轴至少 `4 cm`：

```text
X = [-76, +76] cm
Y = [-76, +76] cm
Z = [  4, 136] cm
```

船舵使用同一规格。源脚本与输出为：

```text
blender/script/python/create_naval_helm_wheel.py
blender/models/SM_Naval_HelmWheel.fbx
blender/models/SM_Naval_HelmWheel.blend
```

当前船舵 FBX 回读包围盒约为 `82 × 50 × 125 cm`（最小点 `(-41,-25,4)`，最大点 `(41,25,129)`），脚本会在导出前检查 160 规格和安全边。舵台的 `CoreSeatCollision` 与炮的 `WeaponCollision` 都使用 `80 × 80 × 70 cm` 半尺寸。

## 2. 建筑模块：200 × 200 × 150 cm

建筑/船体模块的碰撞盒以模块原点为中心：

| 项目 | 约束 |
| --- | --- |
| 完整尺寸 | `200 × 200 × 150 cm` |
| 半尺寸 | `100 × 100 × 75 cm` |
| 碰撞盒中心 | `(0, 0, 0)` |
| 水平吸附单元 | 固定 `200 × 200 cm` |
| 垂直层高 | 固定 `150 cm` |
| 建模安全边 | 各轴至少 `4 cm` |

安全视觉范围为：

```text
X = [-96, +96] cm
Y = [-96, +96] cm
Z = [-71, +71] cm
```

木筏源脚本为：

```text
blender/script/python/SM_Raft.py
blender/models/SM_Raft.fbx
blender/models/SM_Raft.blend
```

脚本导出可见网格 `SM_Raft` 与导入用 `UCX_SM_Raft_00`。FBX 回读结果为可见网格约 `192 × 192 × 131 cm`，UCX 严格为 `200 × 200 × 150 cm`，两者原点均在 `(0,0,0)`。木筏 Actor 使用半尺寸 `(100,100,75)`，视觉偏移为零，浮筒采样点为 `(±80,±80,0)` cm。

## 3. 碰撞、原点与导入责任

1. 应用旋转、缩放、倒角后检查最终合并网格；Blender 使用米，Unreal 使用厘米。
2. 可装配模块的权威碰撞由功能 Actor/组件提供（炮为 `WeaponCollision`，船舵为 `CoreSeatCollision`）。
3. 建筑模块的基础碰撞由宿主 Actor/Fragment 提供；木筏 FBX 中的 UCX 仅用于静态网格导入碰撞。
4. 视觉网格不得依靠 Unreal 组件缩放掩盖源尺寸错误，也不得私自增加改变玩法规则的复杂碰撞。
5. Blender 脚本必须幂等，只删除自己创建的集合，不追加重复对象。

## 4. 模块面吸附网格

- 建筑/船体宿主的水平吸附网格固定为 `200 × 200 cm`，与建筑模块一一对应。
- 宿主垂直层高固定为 `150 cm`；木筏第 0 层承载面为局部 `BaseHeight=75 cm`。
- `ARaftActor::RecomputeGridAlignment()` 每次应用定义时重设 `CellSizeXY=(200,200)` 与 `LevelHeight=150`；即使 `DeckCollision` 因已建结构对称扩张，既有模块的吸附间距也不会改变。
- 200 cm 木筏宿主提供一个中心锚定单元；非整格视觉余数保留为不可建边缘。
- 可装配模块不改变建筑宿主网格步长，且其附着点必须在宿主模块面内，使用自身 160 规格盒校验。
- 吸附位置由宿主根据可建区和层高基准推导，资产脚本不得把甲板厚度、浮筒尺寸等偏移写死进每个模块。

## 5. 提交检查

- [ ] 已明确模块属于可装配 `160×160×140` 或建筑 `200×200×150`。
- [ ] 最终网格完全位于对应安全范围内，各轴至少留 `4 cm`。
- [ ] 原点、单位、轴向、碰撞中心符合所属模块规则。
- [ ] 模型在 `blender/models/`，脚本在 `blender/script/python/`。
- [ ] 脚本可幂等重跑，并已回读检查可见网格和 UCX/运行时碰撞尺寸。
- [ ] 建筑/船体相邻模块以固定 200cm 网格吸附；可装配模块没有扩大宿主网格。
