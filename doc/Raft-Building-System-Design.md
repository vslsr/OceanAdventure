# 木筏建造系统设计方案（Lyra 风格）

> 目标：在现有 `Plugins/GameFeatures/Raft` 的 `ARaftActor` 基础上，支持玩家在木筏上
> **建造 / 拆除扩展区域**（地板、墙、屋顶、功能件），并与浮力、网络同步、存档联动。
> 框架归属通用插件 `Plugins/BuildingCore/`，木筏专属内容归属
> `Plugins/GameFeatures/Raft/`（见 AGENTS.md 与第 1.5 节）。

## 1. Lyra 思想在本系统中的映射

| Lyra 机制 | 在木筏建造中的用法 |
| --- | --- |
| GameFeature + Experience | 建造能力、输入、UI、组件由 GameFeature 的 Action 注入，Experience 决定是否开启建造玩法；框架本身下沉为通用插件（第 1.5 节） |
| DataAsset + Fragment（`ULyraInventoryItemDefinition`） | `UBuildPieceDefinition` + `UBuildPieceFragment_*` 描述每种建造件 |
| ModularGameplay 组件（`UPawnExtensionComponent`） | `UBuildStructureComponent` 挂在 `ARaftActor` 上，走 `UGameFrameworkComponentManager` + InitState 链 |
| FastArraySerializer（`FLyraInventoryList`） | `FBuildPieceList` 增量复制建造件，客户端按回调重建视觉 |
| GAS 能力 + AbilitySet + InputTag | `GA_Build_Mode` / `GA_Build_PlacePiece` / `GA_Build_RemovePiece`，由"锤子"装备（`ULyraEquipmentDefinition`）授予 |
| GameplayMessageSubsystem | `Build.Message.PiecePlaced/PieceRemoved/Failed` 解耦 UI、音效、任务、成就 |
| GameplayTags | 框架用 `Build.Fail.*` / `Build.Message.*` / `InputTag.Build.*`；建造件资产用宿主前缀 `Raft.Piece.Floor.Wood`、`Island.Piece.Wall.Stone` |
| 服务端权威 + 客户端预测表现 | 幽灵预览纯本地；落位一律 Server RPC 校验后写入复制数组 |

## 1.5 模块划分：框架下沉为通用插件

木筏建造与后续的**岛屿静态建筑**共用同一套网格、复制与规则，因此建造框架
**不放在任何 GameFeature 里**，而是与 `OceanCore` 平级的通用运行时插件；
GameFeature 只放“宿主适配 + 资产 + 玩法层”。

```
Plugins/
├─ OceanCore/                通用插件：确定性海面采样（已有）
├─ BuildingCore/             通用插件：宿主无关的建造框架（新增）
└─ GameFeatures/
   ├─ Raft/                  ARaftActor + 浮力 + 宿主适配 + 木筏建造件资产
   ├─ Building/              建造玩法层（P1）：GAS 能力、输入、幽灵、UI
   └─ OceanAdventure/        岛屿地基宿主适配 + 岛屿建造件（后续）

依赖方向：BuildingCore ←── Raft / OceanAdventure / Building
          BuildingCore 不依赖任何 GameFeature，GameFeature 之间没有依赖
```

**为什么框架不做成 GameFeature**：GameFeature 的价值是能被 Experience 独立开关；
一旦 Raft GF 硬依赖 Building GF，这个开关就失效了，而且两个 GF 的模块加载顺序会变成
隐性约束。通用能力下沉到普通插件，正是本工程已有的 `OceanCore` 模式，也符合
`AGENTS.md` 里“可被多个系统共享的通用能力才放入通用插件目录”的规定。

**宿主抽象 `IBuildStructureHost`** 是唯一的差异点：

| | 木筏 `ARaftActor` | 岛屿地基（后续） |
| --- | --- | --- |
| 网格空间 | Actor 局部，随海浪起伏 | Chunk 局部，静止且世界对齐 |
| 挂载点 | `DeckCollision`（MovementBase） | 地形上的静止根组件 |
| 锚定格 | 基础甲板覆盖的格 | 坡度/高度合格的地形格 |
| 连通性 | 必须（否则拆出孤岛会漂散） | 不需要 |
| 结构变化回调 | 重算浮筒与甲板 Box | 空实现 |
| 复制与持久化 | 随 `ARaftActor` 复制 | 随 Chunk 激活 Spawn、静止后 Dormancy |

接口定义与两侧实现见实施手册第 2.5、8 节。

## 2. 数据模型

### 2.1 网格与槽位
木筏采用**局部坐标整数网格**（推荐 cell = 200cm，与 `SM_Raft` 甲板尺寸对齐）：

```cpp
USTRUCT(BlueprintType)
struct FBuildGridCoord   // 木筏局部空间，不受海浪姿态影响
{
    int32 X = 0; int32 Y = 0; int32 Level = 0;  // Level = 楼层
};

UENUM()
enum class EBuildSlotType : uint8 { Foundation, Floor, Wall, Roof, Prop };
```

槽位键 = `{Coord, SlotType, EdgeIndex}`（墙占边、地板占面），保证放置唯一性且
拆除/查询是 O(1) 哈希。**不要**用 Actor 每格一个：数百格时 Actor 开销不可接受。

### 2.2 建造件定义（Fragment 化）

```cpp
UCLASS(BlueprintType, Const)
class UBuildPieceDefinition : public UPrimaryDataAsset
{
    FGameplayTag PieceTag;                       // Raft.Piece.Floor.Wood
    EBuildSlotType SlotType;
    TObjectPtr<UStaticMesh> Mesh;                 // 走 ISM 渲染
    FVector MeshOffset; FIntVector FootprintSize; // 支持 2x1 之类的大件
    TArray<TObjectPtr<UBuildPieceFragment>> Fragments;
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
USTRUCT() struct FBuildPieceEntry : public FFastArraySerializerItem
{
    FBuildGridCoord Coord; EBuildSlotType Slot; uint8 EdgeIndex;
    TObjectPtr<const UBuildPieceDefinition> Definition;
    uint8 Rotation; float Health;
    void PostReplicatedAdd(const FBuildPieceList&);
    void PostReplicatedRemove(const FBuildPieceList&);
};
USTRUCT() struct FBuildPieceList : public FFastArraySerializer { TArray<FBuildPieceEntry> Entries; ... };
```

`UBuildStructureComponent` 持有 `FBuildPieceList`（`Replicated`），并维护一个**仅本地**的
`TMap<FBuildSlotKey, int32>` 索引，服务端和客户端都在 Add/Remove 回调里同步维护。

## 3. 运行时组件

```
ARaftActor
├─ DeckCollision (Box, Root)         // 现有：稳定甲板 + 移动基座
├─ VisualPivot / VisualMesh          // 现有：基础木筏外观
├─ URaftBuoyancyComponent            // 现有：服务端运动学浮力
├─ UBuildStructureComponent  (Replicated) // BuildingCore：结构真值 + 放置/拆除 API
└─ UBuildStructureVisualComponent        // BuildingCore：纯表现，按 Definition 分组管理 ISM + 碰撞

ARaftActor 同时实现 IBuildStructureHost：提供坐标空间、挂载点、锚定格、包围盒回调
```

- **`UBuildStructureComponent`**：唯一真值来源。`CanPlacePiece()` / `ServerPlacePiece()` /
  `ServerRemovePiece()` / `QueryPiece()`；结构变化时广播
  `OnStructureChanged`（本地委托）+ GameplayMessage（跨系统）。
- **`UBuildStructureVisualComponent`**：监听 `OnStructureChanged`，每个 `Mesh` 一个
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
5. 服务端 `UBuildStructureComponent`：重新完整校验（槽位空闲、支撑规则、距离/视线防作弊、
   资源足够 → 从 `ULyraInventoryManagerComponent` 扣除），通过则 `AddEntry` + `MarkItemDirty`。
   失败则广播 `Raft.Message.BuildFailed`（带失败原因 Tag）回给发起者。
6. 拆除 `GA_Raft_RemovePiece`：额外做**连通性校验**——从锚定 Foundation 集合做
   Flood Fill，若移除后出现孤岛则拒绝（或按设计：孤岛整体掉落/沉没）。
   成功后按 `Fragment_BuildCost` 的回收比例返还材料。

所有失败原因用 Tag 表达（`Build.Fail.Occupied` / `.NoSupport` / `.NoResource` /
`.WouldOrphan`），UI 直接订阅消息展示，不做 C++ ↔ UI 硬耦合。

## 5. GameFeature 装配

`Raft` GameFeatureData 中新增 Action（玩法层 Action 在 P1 移到 `Building` GF）：
- `AddComponents`：`ARaftActor` ← `UBuildStructureComponent` + `UBuildStructureVisualComponent`；
  `ALyraCharacter` ← `URaftBuilderComponent`（玩家侧：当前选中件、幽灵、射线）。
- `AddAbilities`：给 Pawn 授予建造 AbilitySet 与 `InputTag.Raft.Build.*` 映射（`ULyraInputConfig`）。
- `AddWidgets`：建造轮盘 / 材料条挂到 Lyra HUD 层。
- `AddGameplayCuePath` / `AddDataRegistry`：建造件 Definition 集合（可用 DataRegistry 做解锁与配方表）。

## 6. 存档与还原
`ARaftActor` 实现存档接口，序列化 `FBuildPieceList`（Definition 用
`FPrimaryAssetId` 而非硬指针）+ 木筏世界变换。载入时按 Level 升序批量
`AddEntry`，全部完成后再触发一次 `OnStructureChanged`（避免 N 次重建 ISM 与浮力）。

## 7. 前置模块与依赖关系

### 7.1 已就绪（直接复用，不需要新写）

| 能力 | 现有落点 | 建造系统怎么用 |
| --- | --- | --- |
| 海面高度/法线采样 | `OceanCoreRuntime`：`UOceanWorldManagerComponent` | 浮力重算、木筏姿态 |
| 木筏本体与浮力 | `Raft` GF：`ARaftActor` / `URaftBuoyancyComponent` / `URaftDefinition` | 建造挂载宿主、结构变化后重算 |
| 背包 | `LyraGame/Inventory`：`ULyraInventoryManagerComponent`（含 `AddItemDefinition` / `ConsumeItemsByDefinition` / 槽位与堆叠） | 扣材料、返还材料 |
| 装备与快捷栏 | `LyraGame/Equipment`：`ULyraEquipmentDefinition` / `ULyraQuickBarComponent` | "建造锤"装备后授予建造 AbilitySet |
| GAS | `LyraGame/AbilitySystem` + `ULyraAbilitySet` | 建造模式、放置、拆除三个能力 |
| 输入 | `ULyraInputConfig` + `InputTag.*` | `InputTag.Raft.Build.Confirm/Cancel/Rotate/Cycle` |
| 消息总线 | `GameplayMessageRouter` | 放置/拆除/失败 → UI、音效、任务 |
| 模式装配 | Experience + GameFeatureAction | 建造能力只在开启建造的 Experience 生效 |
| 作弊/调试入口 | `ULyraCheatManager` + `UCheatManagerExtension`（参考 `ULyraBotCheats`、`ULyraTeamCheats`） | 建造 GM 命令（见第 8 节） |

### 7.2 需要新增（框架在 `BuildingCore`，宿主适配与资产在各自 GF）

0. `BuildingCore` 插件与 `BuildingCoreRuntime` 模块骨架。**最先做。**
1. `BuildGrid` + `IBuildStructureHost`：坐标类型、网格换算、宿主抽象、Debug 绘制。零依赖。
2. `BuildPieceDefinition` + Fragment 集合：数据层，依赖 1。
3. `UBuildStructureComponent`：`FBuildPieceList` FastArray + 放置/拆除/校验，依赖 1、2。
4. `UBuildStructureVisualComponent`：ISM 表现与碰撞，依赖 3。
5. `IBuildResourceSource`：**资源来源抽象**（见 7.3），依赖 2。
6. GAS 能力 + 输入 + 幽灵预览，依赖 3、5。
7. 建造 UI（轮盘/材料条），依赖 3、6。
8. 存档，依赖 3。

### 7.3 背包是不是硬前置？——不是，用接口隔离

建造的资源检查不要直接调 `ULyraInventoryManagerComponent`，而是走一层窄接口：

```cpp
UINTERFACE() class UBuildResourceSource : public UInterface { GENERATED_BODY() };
class IBuildResourceSource
{
    virtual bool HasResources(const TArray<FBuildCost>& Costs) const = 0;
    virtual bool ConsumeResources(const TArray<FBuildCost>& Costs) = 0;   // 服务端调用
    virtual void RefundResources(const TArray<FBuildCost>& Costs) = 0;
};
```

两个实现：

- `UBuildCreativeResourceSource`：永远返回 true，什么都不扣 —— 建造 GM / 单元测试用。
- `UBuildInventoryResourceSource`：转发到 `ULyraInventoryManagerComponent`（`ConsumeItemsByDefinition` / `AddItemDefinition`）—— 正式玩法用。

这样 P0 阶段完全不碰背包也能跑通建造闭环；背包接入只是换一个实现类，`UBuildStructureComponent`
一行不改。同理，**建造 UI 也不是前置**：GM 用 Exec 命令选件，正式玩法用轮盘，两者调同一个
`TryPlacePiece` 入口。

真正的硬前置只有两条：`OceanCore` 的水面采样（已有）和 `ARaftActor` 的稳定甲板与
MovementBase（已有）。

## 8. 建造 GM（先行测试沙盒）

**可以，而且建议先做。** 它让 P0 在没有 UI、没有美术、没有背包的情况下就能验证四件核心事：
网格映射是否正确、FastArray 复制是否正确、角色站在新建地板上是否抖动、结构变化后浮力包围盒
是否正确重算。GM 与正式玩法**共用同一个服务端入口**（`UBuildStructureComponent::TryPlacePiece`），
杜绝"测试路径能跑、正式路径不行"。

### 8.1 作弊命令（Lyra 原生方式）

`UBuildCheats : UCheatManagerExtension`，在 `UBuildStructureComponent::BeginPlay` 里
`CheatManager->AddCheatManagerExtension(...)` 注册（对照 `ULyraBotCheats`）：

| 命令 | 作用 |
| --- | --- |
| `BuildCreative 0/1` | 切换无限材料（切换 `IBuildResourceSource` 实现） |
| `BuildSelect <PieceTag>` | 选中当前建造件，如 `Raft.Piece.Floor.Wood` |
| `BuildPlace [X Y Level]` | 在准星位置或指定格放置（服务端权威） |
| `BuildRemove [X Y Level]` | 拆除 |
| `BuildFill <SizeX> <SizeY>` | 批量铺地板，压测复制与 ISM 数量 |
| `BuildClear` | 清空所有扩展件，回到基础木筏 |
| `BuildDump` | 打印结构表：槽位、定义、支撑关系、连通分量 |
| `BuildDebug 0/1` | 屏幕上绘制网格、占用槽位、支撑箭头、当前包围盒 |

约束：`UFUNCTION(Exec, BlueprintAuthorityOnly)`，Shipping 下随 Lyra 的 CheatManager 一起编译剔除；
客户端输入的命令经 `ServerCheat` 到服务端执行，保证与正式流程一样是服务端权威。

### 8.2 测试 Experience 与地图

- `BP_Experience_RaftBuild_Test`（`LyraExperienceDefinition`）：`GameFeaturesToEnable = [OceanAdventure, Raft]`，
  PawnData 复用 `DA_OceanAdventure_PawnData`，额外挂建造 AbilitySet 与 `UBuildCreativeResourceSource`。
- 地图直接复用 `L_OceanChunkTest`（已有木筏测试 Actor），或加一张 `L_RaftBuildTest`：
  平静海面参数 + 一个 `BP_Raft_Default` + 出生点。
- 与线上玩法的差异**只体现在 Experience 资产上**，代码零分叉。

### 8.3 联机验证清单

用 `Play As Client` + 2 客户端 + Dedicated Server 跑：

1. 客户端 A 放置 → B 与 DS 在同一格出现同一件，且只发增量。
2. 角色站在 A 新建的地板上，木筏随浪起伏时不抖动、不掉落（MovementBase 正确）。
3. 拆除脚下地板 → 角色正常下落，不留幽灵碰撞。
4. `BuildFill 10 10` 后甲板包围盒与 Pontoon 重算正确，浮力不跳变。
5. 断线重连/后加入的客户端能拿到完整结构（初次全量复制）。

## 9. 落地阶段建议
1. **P0 结构核心 + 建造 GM**：`FBuildGridCoord`/`UBuildPieceDefinition`/`UBuildStructureComponent`（含 FastArray）
   + ISM 可视化 + `UBuildCheats` + `UBuildCreativeResourceSource` + 测试 Experience；
   按 8.3 清单验证复制与行走。**此阶段不接背包、不做 UI。**
2. **P1 建造玩法**：GAS 能力、输入、幽灵预览、放置/拆除规则；把资源来源从 Creative 切到
   `UBuildInventoryResourceSource`，接入背包扣料与返还。
3. **P2 系统联动**：浮力/甲板包围盒动态重算、GameplayMessage 驱动 UI 与音效。
4. **P3 扩展**：多层楼梯与屋顶、功能件子 Actor、损坏/维修、存档、鲨鱼破坏事件。

## 10. 性能与网络要点
- 每种 Mesh 一个 ISM；单木筏建议软上限 ~500 件，超出提示玩家。
- FastArray 只传增量；`NetUpdateFrequency` 沿用现有 30/10，结构变化时 `ForceNetUpdate()`。
- 网格判定全部在木筏局部空间进行，与海浪姿态解耦，避免浮点漂移。
- 客户端永不写入 `FBuildPieceList`；预测只体现在幽灵与蒙太奇上。
