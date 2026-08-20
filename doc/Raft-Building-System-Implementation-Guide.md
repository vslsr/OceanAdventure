# 木筏建造系统 · 实施手册（P0）

> 配套设计文档：`doc/Raft-Building-System-Design.md`（讲“为什么这么设计”）。
> 本文只讲“怎么落地”：文件清单、类骨架、算法、验证标准、常见坑。
> **所有新增内容必须位于 `Plugins/GameFeatures/Raft/` 内**（见 `AGENTS.md`）。
> P0 的目标：**不接背包、不做 UI、不做 GAS**，用作弊命令跑通“建造 → 复制 → 行走 → 浮力重算”。

---

## 0. 新增文件清单

```
Plugins/GameFeatures/Raft/Source/RaftRuntime/
├─ Public/Raft/
│  ├─ RaftGridTypes.h            // FRaftGridCoord / FRaftSlotKey / ERaftSlotType + 换算工具
│  ├─ RaftPieceDefinition.h      // URaftPieceDefinition + URaftPieceFragment 基类
│  ├─ RaftPieceCatalog.h         // URaftPieceCatalog：建造件表，复制用索引
│  ├─ RaftBuildComponent.h       // 结构真值 + FRaftPieceList(FastArray) + 放置/拆除
│  ├─ RaftStructureVisualComponent.h  // ISM 表现与碰撞
│  ├─ RaftBuildResourceSource.h  // IRaftBuildResourceSource + Creative 实现
│  └─ RaftBuildCheats.h          // UCheatManagerExtension：建造 GM 命令
├─ Private/Raft/  (同名 .cpp)
└─ RaftRuntime.Build.cs          // 追加依赖
```

`Build.cs` 依赖调整（P0 只加这些，**不要**加 `LyraGame`，背包在 P1 才接）：

```csharp
PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "GameplayTags", "NetCore" });
PrivateDependencyModuleNames.AddRange(new[] { "OceanCoreRuntime" });
```

> `UCheatManagerExtension` 在 `Engine` 模块，P0 不需要额外依赖。

---

## 1. 网格类型（RaftGridTypes.h）

```cpp
UENUM(BlueprintType)
enum class ERaftSlotType : uint8
{
    Foundation, // 浮筒/基础格，占一个 cell 的底面
    Floor,      // 地板，占 cell 的水平面
    Wall,       // 墙，占 cell 的一条边
    Roof,
    Prop        // 功能件（帆、储物箱…）
};

USTRUCT(BlueprintType)
struct FRaftGridCoord
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) int32 X = 0;
    UPROPERTY(EditAnywhere) int32 Y = 0;
    UPROPERTY(EditAnywhere) int32 Level = 0;

    bool operator==(const FRaftGridCoord& O) const { return X==O.X && Y==O.Y && Level==O.Level; }
    friend uint32 GetTypeHash(const FRaftGridCoord& C)
    { return HashCombine(HashCombine(::GetTypeHash(C.X), ::GetTypeHash(C.Y)), ::GetTypeHash(C.Level)); }
};

USTRUCT(BlueprintType)
struct FRaftSlotKey
{
    GENERATED_BODY()
    UPROPERTY() FRaftGridCoord Coord;
    UPROPERTY() ERaftSlotType Slot = ERaftSlotType::Floor;
    UPROPERTY() uint8 EdgeIndex = 0;   // Wall 用 0..3（+X/+Y/-X/-Y），其它类型恒为 0
    bool operator==(const FRaftSlotKey& O) const;
    friend uint32 GetTypeHash(const FRaftSlotKey& K);
};
```

换算（**全部在木筏局部空间**，与海浪姿态解耦）：

```cpp
namespace RaftGrid
{
    constexpr double CellSize    = 200.0;  // cm，与 SM_Raft 甲板模块对齐
    constexpr double LevelHeight = 250.0;  // cm，一层高

    inline FRaftGridCoord LocalToCoord(const FVector& Local)
    {
        return { FMath::FloorToInt(Local.X / CellSize),
                 FMath::FloorToInt(Local.Y / CellSize),
                 FMath::FloorToInt(Local.Z / LevelHeight) };
    }
    inline FVector CoordToLocalCenter(const FRaftGridCoord& C)
    {
        return { (C.X + 0.5) * CellSize, (C.Y + 0.5) * CellSize, C.Level * LevelHeight };
    }
    // 世界坐标进出：用 RaftActor->GetActorTransform().InverseTransformPosition() / TransformPosition()
}
```

**完成标准**：写一个 `RaftBuildDebug 1`，在木筏上画出网格线与准星命中的格子，角色走动时格子编号连续、不跳变。

---

## 2. 建造件定义（RaftPieceDefinition.h / RaftPieceCatalog.h）

```cpp
UCLASS(Abstract, DefaultToInstanced, EditInlineNew)
class URaftPieceFragment : public UObject { GENERATED_BODY() };

UCLASS(BlueprintType, Const)
class RAFTRUNTIME_API URaftPieceDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly) FGameplayTag PieceTag;          // Raft.Piece.Floor.Wood
    UPROPERTY(EditDefaultsOnly) ERaftSlotType SlotType = ERaftSlotType::Floor;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditDefaultsOnly) FVector MeshOffset = FVector::ZeroVector;
    UPROPERTY(EditDefaultsOnly) FIntPoint Footprint = FIntPoint(1, 1);
    UPROPERTY(EditDefaultsOnly, Instanced) TArray<TObjectPtr<URaftPieceFragment>> Fragments;

    const URaftPieceFragment* FindFragmentByClass(TSubclassOf<URaftPieceFragment> Class) const;
};
```

P0 只需要两个 Fragment：`URaftPieceFragment_PlacementRules`（支撑与邻接规则）、
`URaftPieceFragment_Collision`（碰撞盒尺寸、是否可站立）。
`BuildCost` / `Buoyancy` / `UI` / `SpawnActor` 留到 P1~P3。

### 为什么要 Catalog

复制时**不要**直传 `TObjectPtr<URaftPieceDefinition>`，改传 `uint16` 索引：

```cpp
UCLASS(BlueprintType, Const)
class URaftPieceCatalog : public UPrimaryDataAsset
{
    UPROPERTY(EditDefaultsOnly) TArray<TObjectPtr<URaftPieceDefinition>> Pieces;
public:
    const URaftPieceDefinition* GetByIndex(uint16 Index) const;
    bool FindIndex(const URaftPieceDefinition* Def, uint16& OutIndex) const;
    const URaftPieceDefinition* FindByTag(FGameplayTag Tag) const;   // 作弊命令用
};
```

省带宽，且避免定义资产异步加载时序导致客户端拿到空指针。Catalog 由
`URaftBuildComponent` 持有（`EditDefaultsOnly` 硬引用，随组件一起加载）。

---

## 3. 结构组件（RaftBuildComponent.h）——本系统的心脏

### 3.1 FastArray 骨架

```cpp
USTRUCT()
struct FRaftPieceEntry : public FFastArraySerializerItem
{
    GENERATED_BODY()
    UPROPERTY() FRaftSlotKey Key;
    UPROPERTY() uint16 PieceIndex = 0;   // 指向 Catalog
    UPROPERTY() uint8  Rotation   = 0;   // 0..3，90° 步进

    void PostReplicatedAdd(const struct FRaftPieceList& List);
    void PostReplicatedRemove(const struct FRaftPieceList& List);
    void PostReplicatedChange(const struct FRaftPieceList& List);
};

USTRUCT()
struct FRaftPieceList : public FFastArraySerializer
{
    GENERATED_BODY()
    UPROPERTY() TArray<FRaftPieceEntry> Entries;
    UPROPERTY(NotReplicated) TObjectPtr<URaftBuildComponent> OwnerComponent;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    { return FastArrayDeltaSerialize<FRaftPieceEntry, FRaftPieceList>(Entries, DeltaParms, *this); }
};

template<> struct TStructOpsTypeTraits<FRaftPieceList> : public TStructOpsTypeTraitsBase2<FRaftPieceList>
{ enum { WithNetDeltaSerializer = true }; };
```

`URaftBuildComponent` 里：

```cpp
UPROPERTY(Replicated) FRaftPieceList PieceList;

void URaftBuildComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const
{
    Super::GetLifetimeReplicatedProps(Out);
    DOREPLIFETIME(URaftBuildComponent, PieceList);
}
// 构造函数：SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick = false;
// InitializeComponent()/BeginPlay()：PieceList.OwnerComponent = this;
```

### 3.2 对外 API

```cpp
UFUNCTION(BlueprintCallable) bool CanPlacePiece(const FRaftSlotKey& Key, const URaftPieceDefinition* Def, FGameplayTag& OutFailReason) const;
UFUNCTION(BlueprintAuthorityOnly) bool TryPlacePiece(const FRaftSlotKey& Key, const URaftPieceDefinition* Def, uint8 Rotation, AController* Instigator);
UFUNCTION(BlueprintAuthorityOnly) bool TryRemovePiece(const FRaftSlotKey& Key, AController* Instigator);
UFUNCTION(BlueprintPure)         const URaftPieceDefinition* QueryPiece(const FRaftSlotKey& Key) const;
DECLARE_MULTICAST_DELEGATE(FOnRaftStructureChanged);
FOnRaftStructureChanged OnStructureChanged;   // 服务端与客户端都会触发
```

**唯一写入口是 `TryPlacePiece` / `TryRemovePiece`，且带 `check(GetOwner()->HasAuthority())`。**
作弊命令、GAS 能力、存档恢复全部走这两个函数，禁止另开分支。

### 3.3 本地索引

`TMap<FRaftSlotKey, int32> SlotToEntryIndex` —— **不复制**，服务端在写入后重建，
客户端在 `PostReplicatedAdd/Remove` 后重建。删除条目会打乱下标，最稳妥的做法是
结构变化后整表重建索引（P0 规模下开销可忽略），并在同一处触发 `OnStructureChanged`。

### 3.4 放置规则（P0 最小版）

```
CanPlacePiece:
  1. Def 非空、Def->SlotType == Key.Slot                      → Raft.BuildFail.BadDefinition
  2. SlotToEntryIndex 不含 Key（含 Footprint 覆盖的所有格）   → Raft.BuildFail.Occupied
  3. 支撑检查                                                 → Raft.BuildFail.NoSupport
     - Foundation：Level==0，且与基础木筏格或已有 Foundation 4-邻接
     - Floor：同格 Level 有 Foundation，或同层 4-邻接已有 Floor
     - Wall：所在边的两侧至少一格有同层 Floor
  4. 数量上限（建议 500）                                     → Raft.BuildFail.LimitReached
  5. 资源检查（P0 恒 true，走 IRaftBuildResourceSource）      → Raft.BuildFail.NoResource
```

**基础木筏格集合**：由 `URaftDefinition::GetDeckBoxExtent()` 换算出被基础甲板覆盖的
cell 列表，作为常量“锚定格”，既是支撑起点也是连通性 BFS 的起点。

### 3.5 拆除与连通性

```
TryRemovePiece:
  1. 槽位存在且 Def 允许拆除
  2. 连通性预演：把该条目临时摘掉，从锚定格做 BFS/Flood Fill
     （Foundation 与 Floor 之间 4-邻接算连通，Wall/Prop 依附所在格）
     若存在无法到达的条目 → 拒绝，返回 Raft.BuildFail.WouldOrphan
  3. RemoveAtSwap + MarkArrayDirty()，重建索引，触发 OnStructureChanged
```

BFS 每次拆除跑一遍，几百格量级完全够用；不要提前上增量并查集。

---

## 4. 表现层（RaftStructureVisualComponent.h）

- 监听 `OnStructureChanged`（服务端和客户端都执行，服务端也需要碰撞）。
- 每个 `UStaticMesh` 一个运行时创建的 `UInstancedStaticMeshComponent`：

```cpp
UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(GetOwner());
ISM->SetStaticMesh(Mesh);
ISM->SetMobility(EComponentMobility::Movable);
ISM->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
ISM->CanCharacterStepUpOn = ECB_Yes;
ISM->SetCanEverAffectNavigation(false);
ISM->SetupAttachment(RaftActor->GetDeckCollision());   // 随木筏整体运动
ISM->RegisterComponent();
```

- 结构变化时**整表重建实例**（`ClearInstances()` + 批量 `AddInstances`），P0 规模下最简单可靠；
  P2 再优化为增量 `UpdateInstanceTransform` / `RemoveInstance`。
- 实例变换 = `RaftGrid::CoordToLocalCenter(Coord) + Def->MeshOffset`，旋转 = `Rotation * 90°`，
  **相对于 DeckCollision**（`AddInstance(Transform, /*bWorldSpace=*/false)`）。
- 需要独立交互/血量的功能件（帆、舵、储物箱）才生成子 Actor，其余一律 ISM。

---

## 5. 资源来源（RaftBuildResourceSource.h）

```cpp
USTRUCT(BlueprintType)
struct FRaftBuildCost { UPROPERTY(EditDefaultsOnly) TSubclassOf<UObject> ItemDefinition; UPROPERTY(EditDefaultsOnly) int32 Count = 1; };

UINTERFACE(MinimalAPI) class URaftBuildResourceSource : public UInterface { GENERATED_BODY() };
class IRaftBuildResourceSource
{
    GENERATED_BODY()
public:
    virtual bool HasResources(const TArray<FRaftBuildCost>& Costs) const = 0;
    virtual bool ConsumeResources(const TArray<FRaftBuildCost>& Costs) = 0;
    virtual void RefundResources(const TArray<FRaftBuildCost>& Costs) = 0;
};

// P0：无限材料
UCLASS() class URaftCreativeResourceSource : public UObject, public IRaftBuildResourceSource { /* 全部 return true / no-op */ };
```

`URaftBuildComponent` 持有 `TScriptInterface<IRaftBuildResourceSource> ResourceSource`，
默认 `URaftCreativeResourceSource`。P1 换成转发 `ULyraInventoryManagerComponent` 的实现即可，
**组件代码不改**。

---

## 6. 建造 GM（RaftBuildCheats.h）

注册方式抄 `Source/LyraGame/Development/LyraBotCheats.cpp`：

```cpp
// URaftBuildComponent::BeginPlay() 内（仅 !UE_BUILD_SHIPPING）
#if UE_WITH_CHEAT_MANAGER
if (APlayerController* PC = ...; UCheatManager* CM = PC ? PC->CheatManager : nullptr)
{
    CM->AddCheatManagerExtension(NewObject<URaftBuildCheats>(CM));
}
#endif
```

命令清单（全部 `UFUNCTION(Exec, BlueprintAuthorityOnly)`）：

| 命令 | 说明 |
| --- | --- |
| `RaftBuildSelect <PieceTag>` | 选中建造件，Catalog 按 Tag 查 |
| `RaftBuildPlace [X Y Level]` | 省略参数时用准星命中格 |
| `RaftBuildRemove [X Y Level]` | 同上 |
| `RaftBuildFill <SizeX> <SizeY>` | 批量铺地板，压测复制与 ISM |
| `RaftBuildClear` | 清空扩展件回到基础木筏 |
| `RaftBuildDump` | 打印槽位表、支撑关系、连通分量 |
| `RaftBuildDebug 0/1` | 绘制网格、占用槽位、包围盒 |
| `RaftBuildCreative 0/1` | 切换资源来源实现（P1 起有意义） |

客户端敲的命令由 `UCheatManager` 经 `ServerCheat` 转到服务端执行，天然服务端权威。
准星命中：`PC->GetHitResultUnderCursor()`（TopDown）或相机前向 LineTrace，命中 `ARaftActor`
后转局部空间取格。

---

## 7. 与浮力联动

`URaftBuoyancyComponent` 追加：

```cpp
UFUNCTION(BlueprintAuthorityOnly) void RebuildFromStructure(const FBox& LocalStructureBounds, float ExtraMass);
```

服务端在 `OnStructureChanged` 里调用：
- 由所有 Foundation/Floor 格的局部 AABB 求 `LocalStructureBounds`（并入基础甲板 AABB）；
- `DeckCollision->SetBoxExtent(Bounds.GetExtent())` 并把 Box 中心对齐（注意 Box 是以组件原点为中心，
  偏移量要补到 `VisualPivot` 与 ISM 上，或改用相对偏移的子组件承载，**别让根组件跳位**）；
- 由 Bounds 四角生成新的 `PontoonOffsets`；`WaterlineOffset` 按累计质量下压。

客户端不算，靠已有的 `ReplicatedMovement` + 复制的包围盒同步。

---

## 8. GameplayTags 清单

建 `Plugins/GameFeatures/Raft/Config/Tags/RaftTags.ini`（或用 `UE_DEFINE_GAMEPLAY_TAG_STATIC`）：

```
Raft.Piece.Foundation.Wood
Raft.Piece.Floor.Wood
Raft.Piece.Wall.Wood
Raft.Slot.Foundation / Floor / Wall / Roof / Prop
Raft.BuildFail.BadDefinition / Occupied / NoSupport / NoResource / WouldOrphan / LimitReached
Raft.Message.PiecePlaced / PieceRemoved / BuildFailed
Status.Raft.Building                      （P1）
InputTag.Raft.Build.Confirm / Cancel / Rotate / Cycle （P1）
```

---

## 9. 资产与 Experience（沿用现有 Python 约定）

现有脚本在 `Plugins/GameFeatures/Raft/Content/Python/`，保持**幂等**风格：

- `CreateRaftPieceAssets.py` → `DA_RaftPiece_Floor_Wood` 等 + `DA_RaftPieceCatalog`
- `CreateRaftBuildTestExperience.py` → `BP_Experience_RaftBuild_Test`
  （`GameFeaturesToEnable = [OceanAdventure, Raft]`，PawnData 复用 `DA_OceanAdventure_PawnData`）
- 在 Raft 的 GameFeatureData 里加 `AddComponents`：
  `ARaftActor ← URaftBuildComponent` + `URaftStructureVisualComponent`
- 地图直接用 `L_OceanChunkTest`（已有 “Raft Test Actor”）
- `ValidateRaftFeature.py` 追加检查：组件已注入、Catalog 非空、Definition 的 Mesh 非空

---

## 10. 验证清单（P0 完成的判定标准）

Dedicated Server + 2 客户端：

1. `RaftBuildSelect Raft.Piece.Floor.Wood` + `RaftBuildPlace` → 三端同格出现同一件。
2. 抓包/`Net PktLag`：新增一件只发增量，不是整表。
3. 角色站上新建地板，木筏随浪起伏 —— 不抖动、不下陷、不掉出（MovementBase 正确）。
4. 拆掉脚下地板 → 角色正常下落，无残留碰撞。
5. `RaftBuildFill 10 10` → ISM 实例数正确，甲板包围盒与 Pontoon 重算后浮力不跳变。
6. 后加入的客户端拿到完整结构（初次全量复制）。
7. 拆除会造成孤岛时被拒绝，`RaftBuildDump` 中连通分量数恒为 1。

---

## 11. 常见坑

| 现象 | 原因 / 解法 |
| --- | --- |
| 客户端看不到新建件 | 组件未 `SetIsReplicatedByDefault(true)`；或忘了 `DOREPLIFETIME`；或 `TStructOpsTypeTraits` 没写 |
| 只有第一件同步，后续不同步 | 写入后漏了 `MarkItemDirty(Entry)` / `MarkArrayDirty()` |
| 同步有延迟 | 结构变化后调 `GetOwner()->ForceNetUpdate()`；`ARaftActor` 已是 `DORM_Awake`，若改过休眠要 `FlushNetDormancy()` |
| 角色站在新地板上抖动 | ISM 必须挂在 `DeckCollision` 下（同一 MovementBase）；不要挂到 `VisualMesh`（无碰撞） |
| 角色被新建的墙弹飞 | 放置前检查该格是否有 Pawn 重叠，有则拒绝（`Raft.BuildFail.Blocked`） |
| 编辑器里实例翻倍 | `OnConstruction` 重建时先 `ClearInstances()`；或只在 `BeginPlay` 之后才建 ISM |
| 客户端 Definition 为空 | 用 Catalog 索引复制，别直传资产指针 |
| 打包报错找不到 CheatManager | 作弊代码用 `#if UE_WITH_CHEAT_MANAGER` 包裹 |
| 木筏位置突然跳动 | 改 `DeckCollision` 的 BoxExtent 时把中心偏移补到子组件，别动根组件的相对位置 |

---

## 12. 建议提交顺序

1. `RaftGridTypes.h` + Debug 绘制 → 能看到网格
2. `URaftPieceDefinition` / `URaftPieceCatalog` + 两个资产 → 编辑器里可配
3. `URaftBuildComponent`（FastArray + 放置/拆除 + 规则） → `RaftBuildDump` 能打出结构
4. `URaftStructureVisualComponent` → 看得见、走得上去
5. `URaftBuildCheats` + 测试 Experience → 联机跑验证清单
6. 浮力 `RebuildFromStructure` → 大平台不跳变

每步单独一个提交，第 3 步和第 4 步之间务必先用 `RaftBuildDump` 确认数据层正确，
再去调表现层 —— 大多数“看起来是渲染问题”的 bug 其实在复制层。
