# 木筏建造系统设计方案（Lyra 风格）

> 目标：在现有 `Plugins/GameFeatures/Raft` 的 `ARaftActor` 基础上，支持玩家在木筏上
> **建造 / 拆除扩展区域**（地板、墙、屋顶、功能件），并与浮力、网络同步、存档联动。
> 所有内容严格归属 `Plugins/GameFeatures/Raft/`（见 AGENTS.md）。

## 1. Lyra 思想在本系统中的映射

| Lyra 机制 | 在木筏建造中的用法 |
| --- | --- |
| GameFeature + Experience | 建造能力、输入、UI、组件全部由 `Raft` GameFeature 的 Action 注入，Experience 决定是否开启建造玩法 |
| DataAsset + Fragment（`ULyraInventoryItemDefinition`） | `URaftPieceDefinition` + `URaftPieceFragment_*` 描述每种建造件 |
| ModularGameplay 组件（`UPawnExtensionComponent`） | `URaftBuildComponent` 挂在 `ARaftActor` 上，走 `UGameFrameworkComponentManager` + InitState 链 |
| FastArraySerializer（`FLyraInventoryList`） | `FRaftPieceList` 增量复制建造件，客户端按回调重建视觉 |
| GAS 能力 + AbilitySet + InputTag | `GA_Raft_BuildMode` / `GA_Raft_PlacePiece` / `GA_Raft_RemovePiece`，由"锤子"装备（`ULyraEquipmentDefinition`）授予 |
| GameplayMessageSubsystem | `Raft.Message.PiecePlaced/PieceRemoved/BuildFailed` 解耦 UI、音效、任务、成就 |
| GameplayTags | `Raft.Piece.Floor`、`Raft.Slot.Foundation`、`InputTag.Raft.Build.Confirm` 等 |
| 服务端权威 + 客户端预测表现 | 幽灵预览纯本地；落位一律 Server RPC 校验后写入复制数组 |

## 2. 数据模型

### 2.1 网格与槽位
木筏采用**局部坐标整数网格**（推荐 cell = 200cm，与 `SM_Raft` 甲板尺寸对齐）：

```cpp
USTRUCT(BlueprintType)
struct FRaftGridCoord   // 木筏局部空间，不受海浪姿态影响
{
    int32 X = 0; int32 Y = 0; int32 Level = 0;  // Level = 楼层
};

UENUM()
enum class ERaftSlotType : uint8 { Foundation, Floor, Wall, Roof, Prop };
```

槽位键 = `{Coord, SlotType, EdgeIndex}`（墙占边、地板占面），保证放置唯一性且
拆除/查询是 O(1) 哈希。**不要**用 Actor 每格一个：数百格时 Actor 开销不可接受。

### 2.2 建造件定义（Fragment 化）

```cpp
UCLASS(BlueprintType, Const)
class URaftPieceDefinition : public UPrimaryDataAsset
{
    FGameplayTag PieceTag;                       // Raft.Piece.Floor.Wood
    ERaftSlotType SlotType;
    TObjectPtr<UStaticMesh> Mesh;                 // 走 ISM 渲染
    FVector MeshOffset; FIntVector FootprintSize; // 支持 2x1 之类的大件
    TArray<TObjectPtr<URaftPieceFragment>> Fragments;
};
```

常用 Fragment（对应 Lyra 的 `InventoryFragment_*`）：
- `Fragment_BuildCost`：消耗的 `ULyraInventoryItemDefinition` 与数量，供 Ability 的 Cost 检查。
- `Fragment_Buoyancy`：质量、浮力贡献、水线偏移 → 反馈给 `URaftBuoyancyComponent`。
- `Fragment_PlacementRules`：需要的支撑槽位（地板需下方 Foundation/同层相邻地板）、允许的邻接、是否可拆。
- `Fragment_Collision`：碰撞盒尺寸、是否 `CanCharacterStepUpOn`。
- `Fragment_UI`：图标、名称、建造类别（给建造轮盘）。
- `Fragment_SpawnActor`：功能件（帆、舵、锅、储物箱）额外生成的子 Actor 类。

### 2.3 复制状态

```cpp
USTRUCT() struct FRaftPieceEntry : public FFastArraySerializerItem
{
    FRaftGridCoord Coord; ERaftSlotType Slot; uint8 EdgeIndex;
    TObjectPtr<const URaftPieceDefinition> Definition;
    uint8 Rotation; float Health;
    void PostReplicatedAdd(const FRaftPieceList&);
    void PostReplicatedRemove(const FRaftPieceList&);
};
USTRUCT() struct FRaftPieceList : public FFastArraySerializer { TArray<FRaftPieceEntry> Entries; ... };
```

`URaftBuildComponent` 持有 `FRaftPieceList`（`Replicated`），并维护一个**仅本地**的
`TMap<FRaftSlotKey, int32>` 索引，服务端和客户端都在 Add/Remove 回调里同步维护。

## 3. 运行时组件

```
ARaftActor
├─ DeckCollision (Box, Root)         // 现有：稳定甲板 + 移动基座
├─ VisualPivot / VisualMesh          // 现有：基础木筏外观
├─ URaftBuoyancyComponent            // 现有：服务端运动学浮力
├─ URaftBuildComponent  (Replicated) // 新增：结构真值 + 放置/拆除 API
└─ URaftStructureVisualComponent     // 新增：纯表现，按 Definition 分组管理 HISM + 碰撞
```

- **`URaftBuildComponent`**：唯一真值来源。`CanPlacePiece()` / `ServerPlacePiece()` /
  `ServerRemovePiece()` / `QueryPiece()`；结构变化时广播
  `OnStructureChanged`（本地委托）+ GameplayMessage（跨系统）。
- **`URaftStructureVisualComponent`**：监听 `OnStructureChanged`，每个 `Mesh` 一个
  `UInstancedStaticMeshComponent`（附着到 `DeckCollision`，随木筏整体运动）。
  ISM 自带碰撞，角色可正常行走并把木筏根组件当作 MovementBase；
  只有需要独立交互/损坏的功能件才生成真正的子 Actor。

### 与浮力的联动
`OnStructureChanged` → `URaftBuoyancyComponent::RebuildFromStructure()`：
由所有 Foundation 格子的 AABB 重新计算 `DeckCollision` 的 BoxExtent 与
四个（或按包围盒四角动态生成的）`PontoonOffsets`，并按 Fragment_Buoyancy 累加质量
调整 `WaterlineOffset`。这一步仅在服务端执行，客户端通过已有的 ReplicatedMovement +
`DeckBoxExtent` 复制值同步。

## 4. 交互流程（GAS）

1. 玩家从快捷栏装备"建造锤"（`ULyraInventoryItemDefinition` + `InventoryFragment_EquippableItem`），
   `ULyraEquipmentDefinition` 的 AbilitySet 授予 `GA_Raft_BuildMode`。
2. `GA_Raft_BuildMode` 激活：加 `Status.Raft.Building` Tag，推入建造 HUD
   （GameFeatureAction_AddWidgets，Lyra UI 分层），启动本地幽灵预览 Actor。
3. 每帧本地：相机射线 → 命中木筏 → `WorldToGrid()` → `CanPlacePiece()` 本地预判
   → 幽灵材质绿/红。**只读判定，不改状态**。
4. `InputTag.Raft.Build.Confirm` → `GA_Raft_PlacePiece`：
   本地立即播放动画/音效（Cosmetic），`Server` RPC 携带 `{Coord, Slot, Edge, Rotation, DefinitionId}`。
5. 服务端 `URaftBuildComponent`：重新完整校验（槽位空闲、支撑规则、距离/视线防作弊、
   资源足够 → 从 `ULyraInventoryManagerComponent` 扣除），通过则 `AddEntry` + `MarkItemDirty`。
   失败则广播 `Raft.Message.BuildFailed`（带失败原因 Tag）回给发起者。
6. 拆除 `GA_Raft_RemovePiece`：额外做**连通性校验**——从锚定 Foundation 集合做
   Flood Fill，若移除后出现孤岛则拒绝（或按设计：孤岛整体掉落/沉没）。
   成功后按 `Fragment_BuildCost` 的回收比例返还材料。

所有失败原因用 Tag 表达（`Raft.BuildFail.Occupied` / `.NoSupport` / `.NoResource` /
`.WouldOrphan`），UI 直接订阅消息展示，不做 C++ ↔ UI 硬耦合。

## 5. GameFeature 装配

`Raft` GameFeatureData 中新增 Action：
- `AddComponents`：`ARaftActor` ← `URaftBuildComponent` + `URaftStructureVisualComponent`；
  `ALyraCharacter` ← `URaftBuilderComponent`（玩家侧：当前选中件、幽灵、射线）。
- `AddAbilities`：给 Pawn 授予建造 AbilitySet 与 `InputTag.Raft.Build.*` 映射（`ULyraInputConfig`）。
- `AddWidgets`：建造轮盘 / 材料条挂到 Lyra HUD 层。
- `AddGameplayCuePath` / `AddDataRegistry`：建造件 Definition 集合（可用 DataRegistry 做解锁与配方表）。

## 6. 存档与还原
`ARaftActor` 实现存档接口，序列化 `FRaftPieceList`（Definition 用
`FPrimaryAssetId` 而非硬指针）+ 木筏世界变换。载入时按 Level 升序批量
`AddEntry`，全部完成后再触发一次 `OnStructureChanged`（避免 N 次重建 ISM 与浮力）。

## 7. 落地阶段建议
1. **P0 结构核心**：`FRaftGridCoord`/`URaftPieceDefinition`/`URaftBuildComponent`（含 FastArray）
   + ISM 可视化；用控制台命令 `Raft.PlacePiece` 验证复制与行走。
2. **P1 建造玩法**：GAS 能力、输入、幽灵预览、放置/拆除规则、资源消耗。
3. **P2 系统联动**：浮力/甲板包围盒动态重算、GameplayMessage 驱动 UI 与音效。
4. **P3 扩展**：多层楼梯与屋顶、功能件子 Actor、损坏/维修、存档、鲨鱼破坏事件。

## 8. 性能与网络要点
- 每种 Mesh 一个 ISM；单木筏建议软上限 ~500 件，超出提示玩家。
- FastArray 只传增量；`NetUpdateFrequency` 沿用现有 30/10，结构变化时 `ForceNetUpdate()`。
- 网格判定全部在木筏局部空间进行，与海浪姿态解耦，避免浮点漂移。
- 客户端永不写入 `FRaftPieceList`；预测只体现在幽灵与蒙太奇上。
