# 建造系统 · P0 实施手册（代码级）

> 设计依据：`doc/Raft-Building-System-Design.md`。本文给**可直接落地的代码**。
> P0 范围：**不接背包、不做 UI、不写 GAS**，用作弊命令跑通
> “放置 → 复制 → 站上去 → 拆除 → 浮力重算”。

---

## 0. 模块划分（重要：框架与宿主分离）

建造框架**不放在任何 GameFeature 里**，而是作为与 `OceanCore` 平级的通用运行时插件。
理由：木筏建造与后续的岛屿静态建筑要共用同一套网格、复制、规则代码，
而 GameFeature 之间不应产生硬依赖（GF 的意义就是能被 Experience 独立开关）。

```
Plugins/
├─ OceanCore/                      通用插件：确定性海面采样（已有）
├─ BuildingCore/                   通用插件：宿主无关的建造框架  ← 新增
│  └─ Source/BuildingCoreRuntime/
│     ├─ Public/Building/
│     │  ├─ BuildGameplayTags.h
│     │  ├─ BuildGridTypes.h           FBuildGridCoord / FBuildSlotKey / FBuildGridSettings
│     │  ├─ BuildStructureHost.h       IBuildStructureHost（宿主抽象）
│     │  ├─ BuildPieceDefinition.h
│     │  ├─ BuildPieceCatalog.h
│     │  ├─ BuildResourceSource.h
│     │  ├─ BuildStructureComponent.h  结构真值 + FastArray
│     │  ├─ BuildStructureVisualComponent.h
│     │  └─ BuildCheats.h
│     └─ Private/Building/  （同名 .cpp）
└─ GameFeatures/
   ├─ Raft/                        木筏：ARaftActor、浮力、宿主适配、木筏建造件资产
   ├─ Building/                    建造玩法层（P1 才需要）：GAS 能力、输入、幽灵、UI
   └─ OceanAdventure/              岛屿：后续的静态建筑宿主适配、Chunk 持久化
```

依赖方向（**只允许单向，不允许 GF → GF**）：

```
BuildingCore  ←── Raft GF            （ARaftActor 实现 IBuildStructureHost）
      ↑       ←── OceanAdventure GF  （岛屿地基实现 IBuildStructureHost，后续）
      └────── ←── Building GF        （玩法层：能力/输入/UI）
BuildingCore 不依赖 OceanCore、不依赖任何 GameFeature
```

`Plugins/BuildingCore/Source/BuildingCoreRuntime/BuildingCoreRuntime.Build.cs`：

```csharp
public class BuildingCoreRuntime : ModuleRules
{
    public BuildingCoreRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core", "CoreUObject", "Engine", "GameplayTags", "NetCore"
        });
    }
}
```

`Plugins/GameFeatures/Raft/Source/RaftRuntime/RaftRuntime.Build.cs` 追加：

```csharp
PrivateDependencyModuleNames.AddRange(new string[]
{
    "OceanCoreRuntime",
    "BuildingCoreRuntime"     // ← 新增
});
```

> P0 不要加 `LyraGame`（背包在 P1 接）、不要加 `GameplayMessageRuntime`（消息在 P2 接）。
> `UCheatManagerExtension` 在 `Engine` 模块里，无需额外依赖。
> **内容归属**：框架代码在 `BuildingCore`，木筏建造件资产（`DA_BuildPiece_Raft_*`、Catalog）
> 在 `Raft` GF，岛屿建造件资产在 `OceanAdventure` GF —— 符合 `AGENTS.md` 的归属约束。

---

## 1. BuildGameplayTags

**BuildGameplayTags.h**

```cpp
#pragma once
#include "NativeGameplayTags.h"

namespace BuildGameplayTags
{
    BUILDINGCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_BadDefinition);
    BUILDINGCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_Occupied);
    BUILDINGCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_NoSupport);
    BUILDINGCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_NoResource);
    BUILDINGCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_Blocked);
    BUILDINGCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_LimitReached);
    BUILDINGCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_NotFound);
    BUILDINGCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_WouldOrphan);
}
```

**BuildGameplayTags.cpp**

```cpp
#include "Building/BuildGameplayTags.h"

namespace BuildGameplayTags
{
    UE_DEFINE_GAMEPLAY_TAG(Fail_BadDefinition, "Build.Fail.BadDefinition");
    UE_DEFINE_GAMEPLAY_TAG(Fail_Occupied,      "Build.Fail.Occupied");
    UE_DEFINE_GAMEPLAY_TAG(Fail_NoSupport,     "Build.Fail.NoSupport");
    UE_DEFINE_GAMEPLAY_TAG(Fail_NoResource,    "Build.Fail.NoResource");
    UE_DEFINE_GAMEPLAY_TAG(Fail_Blocked,       "Build.Fail.Blocked");
    UE_DEFINE_GAMEPLAY_TAG(Fail_LimitReached,  "Build.Fail.LimitReached");
    UE_DEFINE_GAMEPLAY_TAG(Fail_NotFound,      "Build.Fail.NotFound");
    UE_DEFINE_GAMEPLAY_TAG(Fail_WouldOrphan,   "Build.Fail.WouldOrphan");
}
```

建造件 Tag（`Raft.Piece.Floor.Wood` 等）写在资产里，用 `Config/Tags/RaftTags.ini` 声明即可，
不必进 C++。

---

## 2. BuildGridTypes.h（纯头文件，无 cpp）

```cpp
#pragma once
#include "CoreMinimal.h"
#include "BuildGridTypes.generated.h"

UENUM(BlueprintType)
enum class EBuildSlotType : uint8
{
    Foundation,   // 基础格（浮筒层），Level 恒为 0
    Floor,        // 地板，占整格水平面
    Wall,         // 墙，占格子的一条边
    Roof,
    Prop          // 功能件
};

USTRUCT(BlueprintType)
struct FBuildGridCoord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 X = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Y = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Level = 0;

    FBuildGridCoord() = default;
    FBuildGridCoord(int32 InX, int32 InY, int32 InLevel) : X(InX), Y(InY), Level(InLevel) {}

    bool operator==(const FBuildGridCoord& Other) const
    {
        return X == Other.X && Y == Other.Y && Level == Other.Level;
    }

    FString ToString() const { return FString::Printf(TEXT("(%d,%d,L%d)"), X, Y, Level); }

    friend uint32 GetTypeHash(const FBuildGridCoord& C)
    {
        return HashCombine(HashCombine(::GetTypeHash(C.X), ::GetTypeHash(C.Y)), ::GetTypeHash(C.Level));
    }
};

USTRUCT(BlueprintType)
struct FBuildSlotKey
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) FBuildGridCoord Coord;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EBuildSlotType Slot = EBuildSlotType::Floor;
    /** 仅 Wall 使用：0=+X 1=+Y 2=-X 3=-Y；其它类型恒为 0 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) uint8 EdgeIndex = 0;

    FBuildSlotKey() = default;
    FBuildSlotKey(const FBuildGridCoord& InCoord, EBuildSlotType InSlot, uint8 InEdge = 0)
        : Coord(InCoord), Slot(InSlot), EdgeIndex(InSlot == EBuildSlotType::Wall ? InEdge : 0) {}

    bool operator==(const FBuildSlotKey& Other) const
    {
        return Coord == Other.Coord && Slot == Other.Slot && EdgeIndex == Other.EdgeIndex;
    }

    friend uint32 GetTypeHash(const FBuildSlotKey& K)
    {
        return HashCombine(HashCombine(GetTypeHash(K.Coord), ::GetTypeHash((uint8)K.Slot)),
                           ::GetTypeHash(K.EdgeIndex));
    }
};

/** 每个宿主可以有不同的格子尺寸：木筏 200，岛屿地基可以是 300 */
USTRUCT(BlueprintType)
struct FBuildGridSettings
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, meta = (ClampMin = "10.0", Units = "cm")) double CellSize = 200.0;
    UPROPERTY(EditDefaultsOnly, meta = (ClampMin = "10.0", Units = "cm")) double LevelHeight = 250.0;
};

namespace BuildGrid
{
    inline FBuildGridCoord LocalToCoord(const FVector& Local, const FBuildGridSettings& S)
    {
        return FBuildGridCoord(
            FMath::FloorToInt(Local.X / S.CellSize),
            FMath::FloorToInt(Local.Y / S.CellSize),
            FMath::FloorToInt(Local.Z / S.LevelHeight));
    }

    /** 返回格子中心（XY 居中，Z 取该层地面） */
    inline FVector CoordToLocalCenter(const FBuildGridCoord& C, const FBuildGridSettings& S)
    {
        return FVector((C.X + 0.5) * S.CellSize, (C.Y + 0.5) * S.CellSize, C.Level * S.LevelHeight);
    }

    /** 墙所在边的中心点（相对格子中心的偏移）与朝向 */
    inline FVector EdgeOffset(uint8 EdgeIndex, const FBuildGridSettings& S)
    {
        switch (EdgeIndex & 3)
        {
        case 0:  return FVector( S.CellSize * 0.5, 0.0, 0.0);
        case 1:  return FVector(0.0,  S.CellSize * 0.5, 0.0);
        case 2:  return FVector(-S.CellSize * 0.5, 0.0, 0.0);
        default: return FVector(0.0, -S.CellSize * 0.5, 0.0);
        }
    }
    inline float EdgeYaw(uint8 EdgeIndex) { return 90.f * (EdgeIndex & 3); }

    /** 同层四邻 + 上下同格，用于支撑与连通性 */
    inline void GetNeighbors(const FBuildGridCoord& C, TArray<FBuildGridCoord>& Out)
    {
        Out.Reset(6);
        Out.Add({C.X + 1, C.Y,     C.Level});
        Out.Add({C.X - 1, C.Y,     C.Level});
        Out.Add({C.X,     C.Y + 1, C.Level});
        Out.Add({C.X,     C.Y - 1, C.Level});
        Out.Add({C.X,     C.Y,     C.Level + 1});
        Out.Add({C.X,     C.Y,     C.Level - 1});
    }
}
```

---

## 2.5 IBuildStructureHost —— 宿主抽象（木筏 / 岛屿的唯一差异点）

框架不认识木筏，也不认识岛屿；它只认识“宿主”。宿主提供坐标空间、挂载点、
可建格判定，并接收结构包围盒变化。

**BuildStructureHost.h**

```cpp
#pragma once
#include "UObject/Interface.h"
#include "Building/BuildGridTypes.h"
#include "BuildStructureHost.generated.h"

UINTERFACE(MinimalAPI, NotBlueprintable)
class UBuildStructureHost : public UInterface { GENERATED_BODY() };

class IBuildStructureHost
{
    GENERATED_BODY()

public:
    /** 网格局部空间。木筏 = Actor 变换（随浪运动）；岛屿 = Chunk 的静止变换 */
    virtual FTransform GetStructureSpace() const = 0;

    /** ISM 与碰撞的挂载点。木筏 = DeckCollision（与 MovementBase 同源）；岛屿 = 地基根组件 */
    virtual USceneComponent* GetStructureAttachRoot() const = 0;

    virtual const FBuildGridSettings& GetGridSettings() const = 0;

    /** 该格是否“天然可建”。木筏 = 基础甲板格；岛屿 = 坡度/高度合格的地形格 */
    virtual bool IsCellAnchored(const FBuildGridCoord& Coord) const = 0;

    /** 枚举锚定格，用于连通性 BFS 播种。岛屿可直接返回 false（锚定区无限大） */
    virtual bool CollectAnchorCells(TSet<FBuildGridCoord>& OutCells) const = 0;

    /** 是否要求所有件与锚定区连通。木筏 true（否则拆出的孤岛会漂散）；岛屿 false */
    virtual bool RequiresConnectivity() const { return true; }

    /** 结构包围盒变化。木筏拿去重算浮力与甲板；岛屿空实现 */
    virtual void OnStructureBoundsChanged(const FBox& LocalBounds) {}
};
```

两类宿主的差异一览（除此之外**代码完全共用**）：

| | 木筏（`ARaftActor`） | 岛屿地基（后续 `AIslandFoundationActor`） |
| --- | --- | --- |
| 网格空间 | Actor 局部，随海浪起伏 | Chunk 局部，静止且世界对齐 |
| 挂载点 | `DeckCollision`（MovementBase） | 地形上的静止根组件 |
| 锚定格 | 基础甲板覆盖的格 | 坡度 < 阈值、非水下的地形格 |
| 连通性 | 必须（`RequiresConnectivity = true`） | 不需要 |
| 包围盒回调 | 重算浮筒与甲板 Box | 空实现 |
| 复制/持久化 | 随 `ARaftActor` 复制，随存档序列化 | 随 Chunk 激活 Spawn、静止后 Dormancy（见 `Ocean-Island-Adventure-Design.md` 第 9 节） |

---

## 3. 建造件定义与目录

**BuildPieceDefinition.h**

```cpp
#pragma once
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Building/BuildGridTypes.h"
#include "BuildPieceDefinition.generated.h"

class UStaticMesh;

UCLASS(Abstract, DefaultToInstanced, EditInlineNew)
class BUILDINGCORE_API UBuildPieceFragment : public UObject
{
    GENERATED_BODY()
};

/** 放置规则；缺省即“默认规则”（见 UBuildStructureComponent::CheckSupport） */
UCLASS(DisplayName = "Placement Rules")
class BUILDINGCORE_API UBuildPieceFragment_PlacementRules : public UBuildPieceFragment
{
    GENERATED_BODY()
public:
    /** 是否允许悬空（无支撑）放置，调试用 */
    UPROPERTY(EditDefaultsOnly) bool bAllowFloating = false;
    /** 是否可被玩家拆除 */
    UPROPERTY(EditDefaultsOnly) bool bRemovable = true;
    /** 放置时是否检查该格有无 Pawn */
    UPROPERTY(EditDefaultsOnly) bool bBlockedByPawns = true;
};

/** 碰撞盒；缺省则用整格尺寸 */
UCLASS(DisplayName = "Collision")
class BUILDINGCORE_API UBuildPieceFragment_Collision : public UBuildPieceFragment
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly) bool bBlocking = true;
    UPROPERTY(EditDefaultsOnly) bool bCanCharacterStepUpOn = true;
};

UCLASS(BlueprintType, Const)
class BUILDINGCORE_API UBuildPieceDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, Category = "Building|Piece") FGameplayTag PieceTag;
    UPROPERTY(EditDefaultsOnly, Category = "Building|Piece") EBuildSlotType SlotType = EBuildSlotType::Floor;
    UPROPERTY(EditDefaultsOnly, Category = "Building|Piece") TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditDefaultsOnly, Category = "Building|Piece", meta = (Units = "cm")) FVector MeshOffset = FVector::ZeroVector;
    UPROPERTY(EditDefaultsOnly, Category = "Building|Piece") FIntPoint Footprint = FIntPoint(1, 1);

    UPROPERTY(EditDefaultsOnly, Instanced, Category = "Building|Piece") TArray<TObjectPtr<UBuildPieceFragment>> Fragments;

    template <typename T>
    const T* FindFragment() const
    {
        for (const UBuildPieceFragment* Fragment : Fragments)
        {
            if (const T* Typed = Cast<T>(Fragment)) { return Typed; }
        }
        return nullptr;
    }
};
```

**BuildPieceCatalog.h** —— 复制用索引表，避免直传资产指针

```cpp
#pragma once
#include "Engine/DataAsset.h"
#include "BuildPieceCatalog.generated.h"

class UBuildPieceDefinition;
struct FGameplayTag;

UCLASS(BlueprintType, Const)
class BUILDINGCORE_API UBuildPieceCatalog : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** 顺序即网络索引，**只能追加，禁止插入或删除中间项** */
    UPROPERTY(EditDefaultsOnly, Category = "Building") TArray<TObjectPtr<UBuildPieceDefinition>> Pieces;

    const UBuildPieceDefinition* GetByIndex(uint16 Index) const
    {
        return Pieces.IsValidIndex(Index) ? Pieces[Index].Get() : nullptr;
    }

    bool FindIndex(const UBuildPieceDefinition* Def, uint16& OutIndex) const
    {
        const int32 Index = Pieces.IndexOfByKey(Def);
        if (Index == INDEX_NONE) { return false; }
        OutIndex = static_cast<uint16>(Index);
        return true;
    }

    const UBuildPieceDefinition* FindByTag(FGameplayTag Tag) const;   // cpp 里线性查找即可
};
```

---

## 4. UBuildStructureComponent —— 结构真值

### 4.1 BuildStructureComponent.h

```cpp
#pragma once
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Building/BuildGridTypes.h"
#include "BuildStructureComponent.generated.h"

class UBuildStructureComponent;
class UBuildPieceCatalog;
class UBuildPieceDefinition;
class AController;

USTRUCT()
struct FBuildPieceEntry : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY() FBuildSlotKey Key;
    UPROPERTY() uint16 PieceIndex = 0;   // → UBuildPieceCatalog
    UPROPERTY() uint8  Rotation   = 0;   // 0..3，90° 步进

    void PostReplicatedAdd(const struct FBuildPieceList& InArray);
    void PostReplicatedRemove(const struct FBuildPieceList& InArray);
    void PostReplicatedChange(const struct FBuildPieceList& InArray);
};

USTRUCT()
struct FBuildPieceList : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY() TArray<FBuildPieceEntry> Entries;
    UPROPERTY(NotReplicated) TObjectPtr<UBuildStructureComponent> OwnerComponent = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FBuildPieceEntry, FBuildPieceList>(
            Entries, DeltaParms, *this);
    }
};

template <>
struct TStructOpsTypeTraits<FBuildPieceList> : public TStructOpsTypeTraitsBase2<FBuildPieceList>
{
    enum { WithNetDeltaSerializer = true };
};

DECLARE_MULTICAST_DELEGATE(FOnBuildStructureChanged);

UCLASS(BlueprintType, ClassGroup = (Building), meta = (BlueprintSpawnableComponent))
class BUILDINGCORE_API UBuildStructureComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UBuildStructureComponent();

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // —— 查询（双端可用）——
    UFUNCTION(BlueprintPure, Category = "Building")
    bool CanPlacePiece(const FBuildSlotKey& Key, const UBuildPieceDefinition* Def, FGameplayTag& OutFailReason) const;

    UFUNCTION(BlueprintPure, Category = "Building")
    const UBuildPieceDefinition* QueryPiece(const FBuildSlotKey& Key) const;

    const TArray<FBuildPieceEntry>& GetEntries() const { return PieceList.Entries; }
    const UBuildPieceCatalog* GetCatalog() const { return PieceCatalog; }
    const TSet<FBuildGridCoord>& GetAnchorCells() const { return AnchorCells; }
    const FBuildGridSettings& GetGridSettings() const { return GridSettings; }

    /** 世界坐标 → 槽位；bOutHitRaft 表示是否落在本木筏范围内 */
    UFUNCTION(BlueprintPure, Category = "Building")
    FBuildSlotKey WorldToSlot(const FVector& WorldLocation, EBuildSlotType Slot) const;

    UFUNCTION(BlueprintPure, Category = "Building")
    FVector SlotToWorld(const FBuildSlotKey& Key) const;

    /** 结构的局部包围盒，供浮力/甲板重算 */
    FBox ComputeLocalStructureBounds() const;

    // —— 唯一写入口（服务端）——
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Building")
    bool TryPlacePiece(const FBuildSlotKey& Key, const UBuildPieceDefinition* Def, uint8 Rotation, AController* Instigator);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Building")
    bool TryRemovePiece(const FBuildSlotKey& Key, AController* Instigator);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Building")
    int32 ClearAllPieces();

    /** 结构变化通知（服务端写入后、客户端收到复制后都会触发） */
    FOnBuildStructureChanged OnStructureChanged;

    UPROPERTY(EditDefaultsOnly, Category = "Building")
    TObjectPtr<const UBuildPieceCatalog> PieceCatalog;

    UPROPERTY(EditDefaultsOnly, Category = "Building", meta = (ClampMin = "1"))
    int32 MaxPieceCount = 500;

protected:
    UPROPERTY(Replicated) FBuildPieceList PieceList;

    /** 本地索引，不复制；任何写入/复制回调后整表重建 */
    TMap<FBuildSlotKey, int32> SlotToEntryIndex;

    /** 宿主提供的锚定格，既是支撑起点也是连通性 BFS 种子 */
    TSet<FBuildGridCoord> AnchorCells;

    /** 宿主：ARaftActor / 岛屿地基，BeginPlay 时从 Owner 解析 */
    TScriptInterface<IBuildStructureHost> Host;

    /** 由宿主提供，BeginPlay 时缓存 */
    FBuildGridSettings GridSettings;

    bool ResolveHost();
    bool IsAnchored(const FBuildGridCoord& Coord) const;
    void RebuildAnchorCells();
    void RebuildIndex();
    bool CheckSupport(const FBuildSlotKey& Key, const UBuildPieceDefinition* Def) const;
    bool HasPieceAt(const FBuildGridCoord& Coord, EBuildSlotType Slot) const;
    bool IsCellBlockedByPawn(const FBuildGridCoord& Coord) const;
    bool WouldStayConnectedWithout(int32 SkipEntryIndex) const;
    void NotifyStructureChanged();

    friend struct FBuildPieceEntry;
    void HandleEntriesReplicated();   // 复制回调走这里，合并成一次重建

private:
    bool bRebuildQueued = false;
};
```

### 4.2 BuildStructureComponent.cpp（关键实现）

```cpp
#include "Building/BuildStructureComponent.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Net/UnrealNetwork.h"
#include "Raft/RaftActor.h"
#include "Building/BuildGameplayTags.h"
#include "Building/BuildPieceCatalog.h"
#include "Building/BuildPieceDefinition.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BuildStructureComponent)

// ---------- FastArray 回调：只做“通知”，不做业务 ----------
void FBuildPieceEntry::PostReplicatedAdd(const FBuildPieceList& InArray)
{
    if (InArray.OwnerComponent) { InArray.OwnerComponent->HandleEntriesReplicated(); }
}
void FBuildPieceEntry::PostReplicatedRemove(const FBuildPieceList& InArray)
{
    if (InArray.OwnerComponent) { InArray.OwnerComponent->HandleEntriesReplicated(); }
}
void FBuildPieceEntry::PostReplicatedChange(const FBuildPieceList& InArray)
{
    if (InArray.OwnerComponent) { InArray.OwnerComponent->HandleEntriesReplicated(); }
}

// ---------- 构造与复制 ----------
UBuildStructureComponent::UBuildStructureComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UBuildStructureComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UBuildStructureComponent, PieceList);
}

void UBuildStructureComponent::BeginPlay()
{
    Super::BeginPlay();

    PieceList.OwnerComponent = this;   // 复制回调靠它找回组件（客户端也要走到这里）
    ResolveHost();
    RebuildAnchorCells();
    RebuildIndex();
    NotifyStructureChanged();
}

bool UBuildStructureComponent::ResolveHost()
{
    AActor* Owner = GetOwner();
    if (Owner && Owner->Implements<UBuildStructureHost>())
    {
        Host = TScriptInterface<IBuildStructureHost>(Owner);
        GridSettings = Host->GetGridSettings();
        return true;
    }
    UE_LOG(LogTemp, Error, TEXT("[Build] %s 未实现 IBuildStructureHost，建造组件不会工作"),
           *GetNameSafe(Owner));
    return false;
}
```

**为什么复制回调要延后合并**：一个网络包里可能连续 Add 多条，逐条重建索引与 ISM 是浪费。

```cpp
void UBuildStructureComponent::HandleEntriesReplicated()
{
    if (bRebuildQueued) { return; }
    bRebuildQueued = true;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            bRebuildQueued = false;
            RebuildIndex();
            NotifyStructureChanged();
        }));
    }
}

void UBuildStructureComponent::RebuildIndex()
{
    SlotToEntryIndex.Reset();
    SlotToEntryIndex.Reserve(PieceList.Entries.Num());
    for (int32 Index = 0; Index < PieceList.Entries.Num(); ++Index)
    {
        SlotToEntryIndex.Add(PieceList.Entries[Index].Key, Index);
    }
}

void UBuildStructureComponent::NotifyStructureChanged()
{
    OnStructureChanged.Broadcast();          // 表现层（ISM）监听

    if (Host && GetOwner() && GetOwner()->HasAuthority())
    {
        Host->OnStructureBoundsChanged(ComputeLocalStructureBounds());   // 木筏据此重算浮力
    }
}
```

**锚定格**：由基础甲板的 Box 尺寸换算出 Level 0 的格集合。

```cpp
void UBuildStructureComponent::RebuildAnchorCells()
{
    AnchorCells.Reset();
    if (Host)
    {
        Host->CollectAnchorCells(AnchorCells);   // 木筏枚举甲板格；岛屿可返回空集
    }
}

bool UBuildStructureComponent::IsAnchored(const FBuildGridCoord& Coord) const
{
    // 岛屿的可建区可能很大，不适合枚举 —— 一律问宿主，AnchorCells 只用于 BFS 播种
    return Host ? Host->IsCellAnchored(Coord) : AnchorCells.Contains(Coord);
}
```

木筏侧的实现（在 `Raft` GF 里，见第 8 节）就是把甲板 Box 换算成格集合；
岛屿侧则是按地形坡度判定，`CollectAnchorCells` 直接返回 `false`。

**坐标换算**（注意：全部经 Actor 变换，天生跟随海浪姿态）：

```cpp
FBuildSlotKey UBuildStructureComponent::WorldToSlot(const FVector& WorldLocation, EBuildSlotType Slot) const
{
    if (!Host) { return FBuildSlotKey(); }

    const FVector Local = Host->GetStructureSpace().InverseTransformPosition(WorldLocation);
    FBuildSlotKey Key(BuildGrid::LocalToCoord(Local, GridSettings), Slot);

    if (Slot == EBuildSlotType::Wall)
    {
        // 取离命中点最近的一条边
        const FVector Center = BuildGrid::CoordToLocalCenter(Key.Coord, GridSettings);
        const FVector Delta  = Local - Center;
        Key.EdgeIndex = FMath::Abs(Delta.X) > FMath::Abs(Delta.Y)
            ? (Delta.X > 0 ? 0 : 2)
            : (Delta.Y > 0 ? 1 : 3);
    }
    return Key;
}

FVector UBuildStructureComponent::SlotToWorld(const FBuildSlotKey& Key) const
{
    FVector Local = BuildGrid::CoordToLocalCenter(Key.Coord, GridSettings);
    if (Key.Slot == EBuildSlotType::Wall) { Local += BuildGrid::EdgeOffset(Key.EdgeIndex, GridSettings); }

    return Host ? Host->GetStructureSpace().TransformPosition(Local) : Local;
}
```

**放置校验**（唯一规则来源，客户端预判与服务端复检共用同一函数）：

```cpp
bool UBuildStructureComponent::CanPlacePiece(const FBuildSlotKey& Key, const UBuildPieceDefinition* Def,
                                        FGameplayTag& OutFailReason) const
{
    OutFailReason = FGameplayTag();

    if (!Def || !PieceCatalog) { OutFailReason = BuildGameplayTags::Fail_BadDefinition; return false; }
    if (Def->SlotType != Key.Slot) { OutFailReason = BuildGameplayTags::Fail_BadDefinition; return false; }

    uint16 Unused = 0;
    if (!PieceCatalog->FindIndex(Def, Unused)) { OutFailReason = BuildGameplayTags::Fail_BadDefinition; return false; }

    if (PieceList.Entries.Num() >= MaxPieceCount) { OutFailReason = BuildGameplayTags::Fail_LimitReached; return false; }

    // Footprint 覆盖的每一格都要空
    for (int32 DX = 0; DX < FMath::Max(1, Def->Footprint.X); ++DX)
    {
        for (int32 DY = 0; DY < FMath::Max(1, Def->Footprint.Y); ++DY)
        {
            const FBuildSlotKey Sub(FBuildGridCoord(Key.Coord.X + DX, Key.Coord.Y + DY, Key.Coord.Level),
                                   Key.Slot, Key.EdgeIndex);
            if (SlotToEntryIndex.Contains(Sub)) { OutFailReason = BuildGameplayTags::Fail_Occupied; return false; }
        }
    }

    if (!CheckSupport(Key, Def)) { OutFailReason = BuildGameplayTags::Fail_NoSupport; return false; }

    const UBuildPieceFragment_PlacementRules* Rules = Def->FindFragment<UBuildPieceFragment_PlacementRules>();
    if ((!Rules || Rules->bBlockedByPawns) && Key.Slot != EBuildSlotType::Floor
        && IsCellBlockedByPawn(Key.Coord))
    {
        OutFailReason = BuildGameplayTags::Fail_Blocked;   // 别把玩家封在墙里
        return false;
    }

    return true;
}
```

**支撑规则**（P0 最小可用版，后续加规则只改这一个函数）：

```cpp
bool UBuildStructureComponent::CheckSupport(const FBuildSlotKey& Key, const UBuildPieceDefinition* Def) const
{
    if (const UBuildPieceFragment_PlacementRules* Rules = Def->FindFragment<UBuildPieceFragment_PlacementRules>())
    {
        if (Rules->bAllowFloating) { return true; }
    }

    const FBuildGridCoord& C = Key.Coord;

    switch (Key.Slot)
    {
    case EBuildSlotType::Foundation:
    {
        if (C.Level != 0) { return false; }
        // 与基础木筏或已有 Foundation 4-邻接
        const FBuildGridCoord N[4] = {{C.X+1,C.Y,0},{C.X-1,C.Y,0},{C.X,C.Y+1,0},{C.X,C.Y-1,0}};
        for (const FBuildGridCoord& Coord : N)
        {
            if (IsAnchored(Coord) || HasPieceAt(Coord, EBuildSlotType::Foundation)) { return true; }
        }
        return false;
    }
    case EBuildSlotType::Floor:
    {
        // 同格下方有基础/Foundation/下层地板，或同层 4-邻接已有地板
        if (C.Level == 0 && (IsAnchored(C) || HasPieceAt(C, EBuildSlotType::Foundation))) { return true; }
        if (C.Level > 0 && HasPieceAt(FBuildGridCoord(C.X, C.Y, C.Level - 1), EBuildSlotType::Floor)) { return true; }

        const FBuildGridCoord N[4] = {{C.X+1,C.Y,C.Level},{C.X-1,C.Y,C.Level},
                                     {C.X,C.Y+1,C.Level},{C.X,C.Y-1,C.Level}};
        for (const FBuildGridCoord& Coord : N)
        {
            if (HasPieceAt(Coord, EBuildSlotType::Floor)) { return true; }
        }
        return false;
    }
    case EBuildSlotType::Wall:
    case EBuildSlotType::Roof:
    case EBuildSlotType::Prop:
    default:
        // 依附于本格地板（或基础甲板）
        return HasPieceAt(C, EBuildSlotType::Floor) || (C.Level == 0 && IsAnchored(C));
    }
}

bool UBuildStructureComponent::HasPieceAt(const FBuildGridCoord& Coord, EBuildSlotType Slot) const
{
    return SlotToEntryIndex.Contains(FBuildSlotKey(Coord, Slot));
}

bool UBuildStructureComponent::IsCellBlockedByPawn(const FBuildGridCoord& Coord) const
{
    const UWorld* World = GetWorld();
    const AActor* Owner = GetOwner();
    if (!World || !Owner || !Host) { return false; }

    const FTransform Space = Host->GetStructureSpace();
    const FVector Center = Space.TransformPosition(
        BuildGrid::CoordToLocalCenter(Coord, GridSettings)
        + FVector(0.0, 0.0, GridSettings.LevelHeight * 0.5));
    const FCollisionShape Box = FCollisionShape::MakeBox(
        FVector(GridSettings.CellSize * 0.5, GridSettings.CellSize * 0.5, GridSettings.LevelHeight * 0.5));

    FCollisionQueryParams Params(SCENE_QUERY_STAT(BuildPlacementBlocked), false, Owner);
    return World->OverlapAnyTestByChannel(Center, Space.GetRotation(), ECC_Pawn, Box, Params);
}
```

**放置**：

```cpp
bool UBuildStructureComponent::TryPlacePiece(const FBuildSlotKey& Key, const UBuildPieceDefinition* Def,
                                        uint8 Rotation, AController* Instigator)
{
    AActor* Owner = GetOwner();
    if (!Owner || !Owner->HasAuthority()) { return false; }   // 服务端权威，硬拦

    FGameplayTag FailReason;
    if (!CanPlacePiece(Key, Def, FailReason))
    {
        UE_LOG(LogTemp, Verbose, TEXT("[Build] Place %s rejected: %s"),
               *Key.Coord.ToString(), *FailReason.ToString());
        return false;
    }

    uint16 PieceIndex = 0;
    PieceCatalog->FindIndex(Def, PieceIndex);

    FBuildPieceEntry& NewEntry = PieceList.Entries.AddDefaulted_GetRef();
    NewEntry.Key         = Key;
    NewEntry.PieceIndex  = PieceIndex;
    NewEntry.Rotation    = Rotation & 3;
    PieceList.MarkItemDirty(NewEntry);      // ← 漏了它就只有第一件会同步

    RebuildIndex();
    Owner->ForceNetUpdate();
    NotifyStructureChanged();
    return true;
}
```

**拆除 + 连通性**：

```cpp
bool UBuildStructureComponent::TryRemovePiece(const FBuildSlotKey& Key, AController* Instigator)
{
    AActor* Owner = GetOwner();
    if (!Owner || !Owner->HasAuthority()) { return false; }

    const int32* IndexPtr = SlotToEntryIndex.Find(Key);
    if (!IndexPtr) { return false; }                       // Fail_NotFound

    const FBuildPieceEntry& Entry = PieceList.Entries[*IndexPtr];
    if (const UBuildPieceDefinition* Def = PieceCatalog ? PieceCatalog->GetByIndex(Entry.PieceIndex) : nullptr)
    {
        if (const UBuildPieceFragment_PlacementRules* Rules = Def->FindFragment<UBuildPieceFragment_PlacementRules>())
        {
            if (!Rules->bRemovable) { return false; }
        }
    }

    if (!WouldStayConnectedWithout(*IndexPtr)) { return false; }   // Fail_WouldOrphan

    PieceList.Entries.RemoveAtSwap(*IndexPtr);
    PieceList.MarkArrayDirty();             // ← 删除必须用 MarkArrayDirty，不是 MarkItemDirty

    RebuildIndex();
    Owner->ForceNetUpdate();
    NotifyStructureChanged();
    return true;
}

bool UBuildStructureComponent::WouldStayConnectedWithout(int32 SkipEntryIndex) const
{
    // 岛屿地基不需要全局连通（每件都直接坐在地面上），木筏必须连通，否则会拆出漂散的孤岛
    if (Host && !Host->RequiresConnectivity()) { return true; }

    // 1) 收集移除后剩余的“承重格”
    TSet<FBuildGridCoord> SupportCells;
    for (int32 i = 0; i < PieceList.Entries.Num(); ++i)
    {
        if (i == SkipEntryIndex) { continue; }
        const FBuildSlotKey& K = PieceList.Entries[i].Key;
        if (K.Slot == EBuildSlotType::Foundation || K.Slot == EBuildSlotType::Floor)
        {
            SupportCells.Add(K.Coord);
        }
    }

    // 2) 从锚定格 BFS
    TSet<FBuildGridCoord> Visited;
    TArray<FBuildGridCoord> Queue;
    for (const FBuildGridCoord& Anchor : AnchorCells)
    {
        Visited.Add(Anchor);
        Queue.Add(Anchor);
    }

    TArray<FBuildGridCoord> Neighbors;
    while (Queue.Num() > 0)
    {
        const FBuildGridCoord Current = Queue.Pop(EAllowShrinking::No);
        BuildGrid::GetNeighbors(Current, Neighbors);
        for (const FBuildGridCoord& N : Neighbors)
        {
            if (SupportCells.Contains(N) && !Visited.Contains(N))
            {
                Visited.Add(N);
                Queue.Add(N);
            }
        }
    }

    // 3) 剩余条目必须都站在可达格上
    for (int32 i = 0; i < PieceList.Entries.Num(); ++i)
    {
        if (i == SkipEntryIndex) { continue; }
        const FBuildGridCoord& C = PieceList.Entries[i].Key.Coord;
        if (!Visited.Contains(C) && !IsAnchored(C)) { return false; }
    }
    return true;
}
```

**清空与包围盒**：

```cpp
int32 UBuildStructureComponent::ClearAllPieces()
{
    AActor* Owner = GetOwner();
    if (!Owner || !Owner->HasAuthority()) { return 0; }

    const int32 Count = PieceList.Entries.Num();
    if (Count == 0) { return 0; }

    PieceList.Entries.Reset();
    PieceList.MarkArrayDirty();
    RebuildIndex();
    Owner->ForceNetUpdate();
    NotifyStructureChanged();
    return Count;
}

FBox UBuildStructureComponent::ComputeLocalStructureBounds() const
{
    FBox Bounds(ForceInit);
    const FVector CellExtent(GridSettings.CellSize * 0.5, GridSettings.CellSize * 0.5, 1.0);

    for (const FBuildGridCoord& Anchor : AnchorCells)
    {
        Bounds += FBox::BuildAABB(BuildGrid::CoordToLocalCenter(Anchor, GridSettings), CellExtent);
    }
    for (const FBuildPieceEntry& Entry : PieceList.Entries)
    {
        Bounds += FBox::BuildAABB(BuildGrid::CoordToLocalCenter(Entry.Key.Coord, GridSettings), CellExtent);
    }
    return Bounds;
}
```

---

## 5. UBuildStructureVisualComponent —— ISM 表现与碰撞

**BuildStructureVisualComponent.h**

```cpp
#pragma once
#include "Components/ActorComponent.h"
#include "BuildStructureVisualComponent.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UBuildStructureComponent;

UCLASS(BlueprintType, ClassGroup = (Building), meta = (BlueprintSpawnableComponent))
class BUILDINGCORE_API UBuildStructureVisualComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UBuildStructureVisualComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

protected:
    void RebuildInstances();
    UInstancedStaticMeshComponent* FindOrCreateISM(UStaticMesh* Mesh);

    UPROPERTY(Transient) TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UInstancedStaticMeshComponent>> MeshToISM;
    TWeakObjectPtr<UBuildStructureComponent> BuildComponent;
};
```

**BuildStructureVisualComponent.cpp**

```cpp
void UBuildStructureVisualComponent::BeginPlay()
{
    Super::BeginPlay();

    if (UBuildStructureComponent* Build = GetOwner() ? GetOwner()->FindComponentByClass<UBuildStructureComponent>() : nullptr)
    {
        BuildComponent = Build;
        Build->OnStructureChanged.AddUObject(this, &ThisClass::RebuildInstances);
        RebuildInstances();      // 后加入的客户端首帧就能看到已有结构
    }
}

UInstancedStaticMeshComponent* UBuildStructureVisualComponent::FindOrCreateISM(UStaticMesh* Mesh)
{
    if (TObjectPtr<UInstancedStaticMeshComponent>* Found = MeshToISM.Find(Mesh))
    {
        return *Found;
    }

    // 挂载点由宿主决定：木筏 = DeckCollision，岛屿 = 地基根组件
    IBuildStructureHost* HostPtr = Cast<IBuildStructureHost>(GetOwner());
    USceneComponent* AttachTo = HostPtr ? HostPtr->GetStructureAttachRoot() : nullptr;
    if (!AttachTo) { return nullptr; }

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(GetOwner());
    ISM->SetStaticMesh(Mesh);
    ISM->SetMobility(EComponentMobility::Movable);
    ISM->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
    ISM->CanCharacterStepUpOn = ECB_Yes;             // 角色能站上去
    ISM->SetGenerateOverlapEvents(false);
    ISM->SetCanEverAffectNavigation(false);
    ISM->SetupAttachment(AttachTo);                  // ← 木筏必须挂 DeckCollision，与 MovementBase 同源
    ISM->RegisterComponent();
    ISM->SetRelativeTransform(FTransform::Identity); // 实例变换 == 木筏局部坐标

    MeshToISM.Add(Mesh, ISM);
    return ISM;
}

void UBuildStructureVisualComponent::RebuildInstances()
{
    UBuildStructureComponent* Build = BuildComponent.Get();
    if (!Build) { return; }

    for (TPair<TObjectPtr<UStaticMesh>, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : MeshToISM)
    {
        Pair.Value->ClearInstances();     // P0 整表重建：最简单可靠；P2 再改增量
    }

    const UBuildPieceCatalog* Catalog = Build->GetCatalog();
    if (!Catalog) { return; }

    for (const FBuildPieceEntry& Entry : Build->GetEntries())
    {
        const UBuildPieceDefinition* Def = Catalog->GetByIndex(Entry.PieceIndex);
        if (!Def || !Def->Mesh) { continue; }

        UInstancedStaticMeshComponent* ISM = FindOrCreateISM(Def->Mesh);
        if (!ISM) { continue; }

        const FBuildGridSettings& S = Build->GetGridSettings();
        FVector Location = BuildGrid::CoordToLocalCenter(Entry.Key.Coord, S) + Def->MeshOffset;
        float Yaw = 90.f * Entry.Rotation;
        if (Entry.Key.Slot == EBuildSlotType::Wall)
        {
            Location += BuildGrid::EdgeOffset(Entry.Key.EdgeIndex, S);
            Yaw = BuildGrid::EdgeYaw(Entry.Key.EdgeIndex);
        }

        ISM->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), Location), /*bWorldSpace=*/false);
    }
}
```

---

## 6. 资源来源接口（背包解耦点）

**BuildResourceSource.h**

```cpp
#pragma once
#include "UObject/Interface.h"
#include "BuildResourceSource.generated.h"

USTRUCT(BlueprintType)
struct FBuildCost
{
    GENERATED_BODY()
    /** P1 接背包时改成 TSubclassOf<ULyraInventoryItemDefinition> */
    UPROPERTY(EditDefaultsOnly) TSoftClassPtr<UObject> ItemDefinition;
    UPROPERTY(EditDefaultsOnly, meta = (ClampMin = "1")) int32 Count = 1;
};

UINTERFACE(MinimalAPI, NotBlueprintable)
class UBuildResourceSource : public UInterface { GENERATED_BODY() };

class IBuildResourceSource
{
    GENERATED_BODY()
public:
    virtual bool HasResources(const TArray<FBuildCost>& Costs) const = 0;
    virtual bool ConsumeResources(const TArray<FBuildCost>& Costs) = 0;
    virtual void RefundResources(const TArray<FBuildCost>& Costs) = 0;
};

/** P0：无限材料 */
UCLASS()
class BUILDINGCORE_API UBuildCreativeResourceSource : public UObject, public IBuildResourceSource
{
    GENERATED_BODY()
public:
    virtual bool HasResources(const TArray<FBuildCost>&) const override { return true; }
    virtual bool ConsumeResources(const TArray<FBuildCost>&) override { return true; }
    virtual void RefundResources(const TArray<FBuildCost>&) override {}
};
```

P1 只需新增 `URaftInventoryResourceSource`（内部持有
`ULyraInventoryManagerComponent*`，转发 `ConsumeItemsByDefinition` / `AddItemDefinition`），
在 `UBuildStructureComponent` 里换一个 `TScriptInterface` 赋值，**建造逻辑一行不改**。

---

## 7. 建造 GM：UBuildCheats

**BuildCheats.h**

```cpp
#pragma once
#include "GameFramework/CheatManager.h"
#include "Building/BuildGridTypes.h"
#include "BuildCheats.generated.h"

class ARaftActor;
class UBuildStructureComponent;
class UBuildPieceDefinition;

UCLASS(NotBlueprintable)
class UBuildCheats final : public UCheatManagerExtension
{
    GENERATED_BODY()

public:
    UBuildCheats();

    UFUNCTION(Exec) void BuildSelect(const FString& PieceTag);
    UFUNCTION(Exec, BlueprintAuthorityOnly) void BuildPlace(int32 X = MIN_int32, int32 Y = 0, int32 Level = 0);
    UFUNCTION(Exec, BlueprintAuthorityOnly) void BuildRemove(int32 X = MIN_int32, int32 Y = 0, int32 Level = 0);
    UFUNCTION(Exec, BlueprintAuthorityOnly) void BuildFill(int32 SizeX, int32 SizeY);
    UFUNCTION(Exec, BlueprintAuthorityOnly) void BuildClear();
    UFUNCTION(Exec) void BuildDump();
    UFUNCTION(Exec) void BuildDebug(int32 bEnabled);

private:
    UBuildStructureComponent* GetBuildComponent() const;
    const UBuildPieceDefinition* GetSelectedPiece() const;
    bool GetCursorSlot(FBuildSlotKey& OutKey) const;

    FString SelectedPieceTag = TEXT("Raft.Piece.Floor.Wood");
};
```

**BuildCheats.cpp**（注册方式与 `ULyraBotCheats` 完全一致）

```cpp
#include "Building/BuildCheats.h"

#include "GameFramework/CheatManagerDefines.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Building/BuildStructureComponent.h"
#include "Building/BuildPieceCatalog.h"
#include "Building/BuildPieceDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BuildCheats)

UBuildCheats::UBuildCheats()
{
#if UE_WITH_CHEAT_MANAGER
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        UCheatManager::RegisterForOnCheatManagerCreated(FOnCheatManagerCreated::FDelegate::CreateLambda(
            [](UCheatManager* CheatManager)
            {
                CheatManager->AddCheatManagerExtension(NewObject<ThisClass>(CheatManager));
            }));
    }
#endif
}

UBuildStructureComponent* UBuildCheats::GetBuildComponent() const
{
#if UE_WITH_CHEAT_MANAGER
    APlayerController* PC = GetPlayerController();
    if (!PC) { return nullptr; }

    // 1) 优先取角色脚下的宿主（木筏用 MovementBase，岛屿地基同理）
    if (const APawn* Pawn = PC->GetPawn())
    {
        if (const UPrimitiveComponent* Base = Pawn->GetMovementBase())
        {
            if (AActor* BaseOwner = Base->GetOwner())
            {
                if (UBuildStructureComponent* Build = BaseOwner->FindComponentByClass<UBuildStructureComponent>())
                {
                    return Build;
                }
            }
        }
    }

    // 2) 退化：取最近的一个宿主。框架不认识 ARaftActor，只按组件找
    UBuildStructureComponent* Best = nullptr;
    double BestDistSq = TNumericLimits<double>::Max();
    const FVector From = PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector;
    for (TObjectIterator<UBuildStructureComponent> It; It; ++It)
    {
        UBuildStructureComponent* Comp = *It;
        if (!IsValid(Comp) || Comp->GetWorld() != PC->GetWorld()) { continue; }
        const double DistSq = FVector::DistSquared(From, Comp->GetOwner()->GetActorLocation());
        if (DistSq < BestDistSq) { BestDistSq = DistSq; Best = Comp; }
    }
    return Best;
#endif
    return nullptr;
}

bool UBuildCheats::GetCursorSlot(FBuildSlotKey& OutKey) const
{
#if UE_WITH_CHEAT_MANAGER
    APlayerController* PC = GetPlayerController();
    UBuildStructureComponent* Build = GetBuildComponent();
    if (!PC || !Build) { return false; }

    FHitResult Hit;
    // TopDown 用光标；第一人称/第三人称改成相机前向 LineTrace
    if (PC->GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), false, Hit)
        && Hit.bBlockingHit)
    {
        const UBuildPieceDefinition* Def = GetSelectedPiece();
        OutKey = Build->WorldToSlot(Hit.ImpactPoint, Def ? Def->SlotType : EBuildSlotType::Floor);
        return true;
    }
#endif
    return false;
}

void UBuildCheats::BuildPlace(int32 X, int32 Y, int32 Level)
{
#if UE_WITH_CHEAT_MANAGER
    UBuildStructureComponent* Build = GetBuildComponent();
    const UBuildPieceDefinition* Def = GetSelectedPiece();
    if (!Build || !Def) { return; }

    FBuildSlotKey Key;
    if (X == MIN_int32)                       // 未传坐标 → 用准星
    {
        if (!GetCursorSlot(Key)) { return; }
    }
    else
    {
        Key = FBuildSlotKey(FBuildGridCoord(X, Y, Level), Def->SlotType);
    }

    Build->TryPlacePiece(Key, Def, /*Rotation=*/0, GetPlayerController());
#endif
}

void UBuildCheats::BuildFill(int32 SizeX, int32 SizeY)
{
#if UE_WITH_CHEAT_MANAGER
    UBuildStructureComponent* Build = GetBuildComponent();
    const UBuildPieceDefinition* Def = GetSelectedPiece();
    if (!Build || !Def) { return; }

    int32 Placed = 0;
    for (int32 X = 0; X < SizeX; ++X)
    {
        for (int32 Y = 0; Y < SizeY; ++Y)
        {
            // 从锚定区外沿逐圈扩散，保证每一步都有支撑
            if (Build->TryPlacePiece(FBuildSlotKey(FBuildGridCoord(X, Y, 0), Def->SlotType), Def, 0,
                                     GetPlayerController()))
            {
                ++Placed;
            }
        }
    }
    UE_LOG(LogTemp, Log, TEXT("[Build] Fill placed %d pieces"), Placed);
#endif
}

void UBuildCheats::BuildDump()
{
#if UE_WITH_CHEAT_MANAGER
    const UBuildStructureComponent* Build = GetBuildComponent();
    if (!Build) { return; }

    UE_LOG(LogTemp, Log, TEXT("[Build] %d anchors, %d pieces"),
           Build->GetAnchorCells().Num(), Build->GetEntries().Num());
    for (const FBuildPieceEntry& Entry : Build->GetEntries())
    {
        const UBuildPieceDefinition* Def = Build->GetCatalog()->GetByIndex(Entry.PieceIndex);
        UE_LOG(LogTemp, Log, TEXT("  %s slot=%d edge=%d rot=%d def=%s"),
               *Entry.Key.Coord.ToString(), (int32)Entry.Key.Slot, Entry.Key.EdgeIndex, Entry.Rotation,
               Def ? *Def->PieceTag.ToString() : TEXT("<null>"));
    }
#endif
}
```

> `BuildFill` 按 X 外层、Y 内层扫描时，第一列之外的格子可能一时无支撑而被拒。
> 简单做法是循环两遍，或按“到锚定区曼哈顿距离”排序后再放；调试命令不必追求最优。

`BuildDebug` 在 `UBuildStructureComponent` 里配一个 `bDrawDebug` + `DrawDebugBox/DrawDebugString`，
在 `TickComponent`（调试时才开 tick）或 `UDebugDrawService` 回调里画：
锚定格白框、已占格绿框、当前准星格黄框、结构包围盒红框。

---

## 8. Raft 侧适配（`Raft` GameFeature 内，唯一与木筏相关的代码）

`ARaftActor` 实现宿主接口，把“木筏是什么”告诉框架。

**RaftActor.h**

```cpp
#include "Building/BuildStructureHost.h"

UCLASS(BlueprintType, Blueprintable)
class RAFTRUNTIME_API ARaftActor : public AActor, public IBuildStructureHost
{
    GENERATED_BODY()
public:
    // —— IBuildStructureHost ——
    virtual FTransform GetStructureSpace() const override { return GetActorTransform(); }
    virtual USceneComponent* GetStructureAttachRoot() const override { return DeckCollision; }
    virtual const FBuildGridSettings& GetGridSettings() const override { return GridSettings; }
    virtual bool IsCellAnchored(const FBuildGridCoord& Coord) const override;
    virtual bool CollectAnchorCells(TSet<FBuildGridCoord>& OutCells) const override;
    virtual bool RequiresConnectivity() const override { return true; }
    virtual void OnStructureBoundsChanged(const FBox& LocalBounds) override;

protected:
    UPROPERTY(EditAnywhere, Category = "Raft|Build") FBuildGridSettings GridSettings;   // CellSize=200
};
```

**RaftActor.cpp**

```cpp
bool ARaftActor::CollectAnchorCells(TSet<FBuildGridCoord>& OutCells) const
{
    if (!DeckCollision) { return false; }

    // 用“基础甲板”的初始尺寸，不要用被建造撑大后的当前尺寸，否则锚定区会自增长
    const FVector Extent = RaftDefinition ? RaftDefinition->GetDeckBoxExtent()
                                          : DeckCollision->GetUnscaledBoxExtent();
    const FBuildGridCoord Min = BuildGrid::LocalToCoord(FVector(-Extent.X, -Extent.Y, 0.0), GridSettings);
    const FBuildGridCoord Max = BuildGrid::LocalToCoord(
        FVector(Extent.X - KINDA_SMALL_NUMBER, Extent.Y - KINDA_SMALL_NUMBER, 0.0), GridSettings);

    for (int32 X = Min.X; X <= Max.X; ++X)
    {
        for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
        {
            OutCells.Add(FBuildGridCoord(X, Y, 0));
        }
    }
    return true;
}

bool ARaftActor::IsCellAnchored(const FBuildGridCoord& Coord) const
{
    if (Coord.Level != 0) { return false; }
    TSet<FBuildGridCoord> Cells;
    CollectAnchorCells(Cells);          // 量很小；嫌慢就在 BeginPlay 缓存一份
    return Cells.Contains(Coord);
}

void ARaftActor::OnStructureBoundsChanged(const FBox& LocalBounds)
{
    if (!HasAuthority() || !LocalBounds.IsValid) { return; }

    BuoyancyComponent->RebuildFromStructure(LocalBounds);

    // 甲板碰撞跟着长大。BoxComponent 以自身原点为中心，结构不对称时**不要动根组件位置**，
    // 把偏移补到浮筒采样点与 ISM 上；直接移动根组件会让整条木筏瞬移，客户端能看到跳变。
    const FVector Extent = LocalBounds.GetExtent();
    DeckCollision->SetBoxExtent(FVector(Extent.X, Extent.Y,
                                        DeckCollision->GetUnscaledBoxExtent().Z));
}
```

**RaftBuoyancyComponent 新增**：

```cpp
UFUNCTION(BlueprintAuthorityOnly, Category = "Raft|Buoyancy")
void RebuildFromStructure(const FBox& LocalStructureBounds);

void URaftBuoyancyComponent::RebuildFromStructure(const FBox& LocalBounds)
{
    if (!GetOwner()->HasAuthority() || !LocalBounds.IsValid) { return; }

    const FVector Center = LocalBounds.GetCenter();
    const FVector Extent = LocalBounds.GetExtent();
    const double Inset = 0.85;   // 内缩，避免采样点落到结构外

    PontoonOffsets.Reset(4);
    PontoonOffsets.Add(Center + FVector( Extent.X * Inset,  Extent.Y * Inset, 0.0));
    PontoonOffsets.Add(Center + FVector( Extent.X * Inset, -Extent.Y * Inset, 0.0));
    PontoonOffsets.Add(Center + FVector(-Extent.X * Inset,  Extent.Y * Inset, 0.0));
    PontoonOffsets.Add(Center + FVector(-Extent.X * Inset, -Extent.Y * Inset, 0.0));
}
```

> 岛屿宿主（后续）只需实现同样五个函数：`GetStructureSpace` 返回 Chunk 变换、
> `IsCellAnchored` 按地形坡度判定、`CollectAnchorCells` 返回 `false`、
> `RequiresConnectivity` 返回 `false`、`OnStructureBoundsChanged` 空实现。
> **框架代码一行不改。**

---

## 9. 资产归属与 Experience

**内容归属**（`AGENTS.md` 硬约束）：

| 内容 | 位置 |
| --- | --- |
| 框架 C++（网格、组件、Catalog 类、作弊命令） | `Plugins/BuildingCore/` |
| 木筏建造件资产 `DA_BuildPiece_Raft_Floor_Wood` 等 + `DA_BuildPieceCatalog_Raft` | `Plugins/GameFeatures/Raft/Content/Build/` |
| `ARaftActor` 宿主实现、浮力 | `Plugins/GameFeatures/Raft/Source/RaftRuntime/` |
| 岛屿建造件与地基宿主（后续） | `Plugins/GameFeatures/OceanAdventure/` |
| 建造玩法层（GAS/输入/UI，P1） | `Plugins/GameFeatures/Building/` |

> **Catalog 的索引只在本宿主内有效**：木筏一份 Catalog、岛屿一份 Catalog，
> 各自的 uint16 索引互不相干。这正好是分插件后的自然结果，不需要全局注册表。

Python 脚本（放 `Plugins/GameFeatures/Raft/Content/Python/`，保持幂等风格）：

- `CreateRaftBuildPieceAssets.py`：三个建造件 + Catalog（P0 可先用 `SM_Raft` 缩放代替美术资产）。
- `CreateRaftBuildTestExperience.py`：`BP_Experience_RaftBuild_Test`，
  `GameFeaturesToEnable = [OceanAdventure, Raft]`，PawnData 复用 `DA_OceanAdventure_PawnData`。
- Raft 的 `GameFeatureData` 追加 `AddComponents`：
  `ARaftActor ← UBuildStructureComponent`、`ARaftActor ← UBuildStructureVisualComponent`。
- `ValidateRaftFeature.py` 追加：组件已注入、Catalog 非空、每个 Definition 的 Mesh 非空、
  `ARaftActor` 实现了 `IBuildStructureHost`。

地图直接用现成的 `L_OceanChunkTest`（已放置 “Raft Test Actor”）。

---

## 10. 验证清单（P0 判定标准）

Dedicated Server + 2 客户端：

1. `BuildSelect Raft.Piece.Floor.Wood` → `BuildPlace` → 三端同格出现同一件。
2. `Net PktLag=200`：新增一件只发增量，不是整表。
3. 角色站上新建地板，木筏随浪起伏 —— 不抖动、不下陷（MovementBase 正确）。
4. 拆掉脚下地板 → 角色正常下落，无残留碰撞。
5. `BuildFill 10 10` → ISM 实例数正确，浮筒重算后浮力不跳变。
6. 后加入的客户端拿到完整结构（首次全量复制 + `BeginPlay` 里的 `RebuildInstances`）。
7. 造成孤岛的拆除被拒绝，`BuildDump` 中所有条目都可从锚定格到达。

---

## 11. 常见坑（按出现频率排序）

| 现象 | 原因 / 解法 |
| --- | --- |
| 只有第一件同步 | 写入后漏 `MarkItemDirty(Entry)`；删除要用 `MarkArrayDirty()` |
| 客户端完全收不到 | 组件构造漏 `SetIsReplicatedByDefault(true)`；或漏 `DOREPLIFETIME`；或 `TStructOpsTypeTraits` 没写 `WithNetDeltaSerializer` |
| 复制回调里 `OwnerComponent` 为空 | `PieceList.OwnerComponent = this` 必须在 `BeginPlay`（客户端也要执行）里赋值 |
| 客户端 Definition 为 null | 别直传资产指针，用 Catalog 索引；Catalog 用硬引用 `EditDefaultsOnly` |
| 角色站上去抖动/被弹开 | ISM 必须 `SetupAttachment(DeckCollision)`，与 MovementBase 同源；不要挂到无碰撞的 `VisualMesh` |
| 同步延迟明显 | 结构变化后 `ForceNetUpdate()`；若改过休眠还要 `FlushNetDormancy()` |
| 编辑器里实例翻倍 | `RebuildInstances` 开头必须 `ClearInstances()`；ISM 只在运行时创建，别在 `OnConstruction` 建 |
| 木筏突然瞬移 | 改 `DeckCollision`（根组件）尺寸时动了相对位置，见第 8 节 |
| 打包 Shipping 报错 | 作弊代码全部 `#if UE_WITH_CHEAT_MANAGER` 包裹 |
| 建造组件完全不工作、日志报未实现宿主 | 宿主 Actor 忘了继承 `IBuildStructureHost`，或 `GameFeatureAction_AddComponents` 把组件加到了非宿主 Actor 上 |
| `BuildingCore` 里出现 `#include "Raft/..."` | 方向反了。框架永远不认识宿主，只能通过 `IBuildStructureHost` 反问 |
| 拆除后索引错乱 | `RemoveAtSwap` 会打乱下标，必须 `RebuildIndex()` 整表重建 |

---

## 12. 建议提交顺序（每步一个 commit）

0. 建 `Plugins/BuildingCore` 空插件 + `BuildingCoreRuntime` 模块，`Raft` 加依赖 → 能编过
1. `BuildGameplayTags` + `BuildGridTypes.h` + `IBuildStructureHost` → 定义好边界
2. `UBuildPieceDefinition` + `UBuildPieceCatalog` + 三个资产（放 Raft GF）→ 编辑器里可配
3. `UBuildStructureComponent`（FastArray + 规则 + 连通性）+ `ARaftActor` 实现宿主接口
   → `BuildDump` 打得出结构
4. `UBuildStructureVisualComponent` → 看得见、走得上去
5. `UBuildCheats` + 测试 Experience → 联机跑第 10 节清单
6. `RebuildFromStructure` + 甲板包围盒 → 大平台不跳变

第 3 步到第 4 步之间务必先用 `BuildDump` 确认数据层正确再调表现层：
绝大多数“看起来是渲染问题”的 bug，根子都在复制层。

---

## 13. P0 修复记录（对齐 / 闪屏 / 吸附）

实现落地后发现的问题与修法，代码以仓库为准，本节只记结论。

### 13.1 网格对齐

- `FBuildGridSettings` 新增 `CellOrigin`（XY 网格线偏移）与 `BaseHeight`（Level 0 的地面高度）。
  `LocalToCoord` / `CoordToLocalCenter` 都按这两个值换算。
- `ARaftActor::RecomputeGridAlignment()`（`PostInitializeComponents` 与 `ApplyDefinition` 中调用）
  按甲板尺寸算出**能完整放进甲板的整格数**：奇数格自动加半格偏移保持居中，
  余数留作不可建的视觉边缘。甲板 248×400 + 200 格 → 1×2 格（200×400），
  不再出现建造件探出甲板 76cm 的情况，**不需要改美术资产**。
- `BaseHeight = 甲板半高`，所以建造件的 `MeshOffset.Z` 只需补自己的半高
  （由 `CreateRaftBuildPieceAssets.py` 的 `cell_bottom_offset()` 自动算），
  不再把甲板厚度硬编码进每个资产。
- `IsCellAnchored` 改成 O(1) 的索引区间判断，不再每次构造 `TSet`。

### 13.2 甲板碰撞盒

- `NotifyStructureChanged` 传给宿主的是 `ComputePieceBounds()`（**只含建造件**），
  不再包含锚定格 —— 之前空结构在 `BeginPlay` 就把甲板从 248×400 撑成 400×400。
- `ARaftActor::OnStructureBoundsChanged` 以 `RaftDefinition` 的甲板尺寸为下限，
  只在尺寸真正变化时才 `SetBoxExtent`（根组件是角色的 MovementBase，每次改都会重新落地）。

### 13.3 闪屏

| 原因 | 修法 |
| --- | --- |
| 非法时预览跳到未吸附位置 | 预览恒定停在吸附格，只换材质 |
| 失败分支每帧 `DestroyPreviewMesh` 再重建 | 改为 `HidePreview()`（只切可见性） |
| valid/invalid 每帧翻转导致材质反复重建 | 只在状态真正跳变时换材质；`bLastAppliedValid` 缓存 |
| 木筏随浪起伏导致格子来回跳 | 射线改打 Level 0 平面（甲板顶面）+ `SlotSwitchHysteresis` 迟滞 |
| 幽灵材质 `disable_depth_test = True` 盖住全屏 | 关闭；改用 `PreviewLiftZ` 抬高 1cm 防 z-fighting |

### 13.4 甲板吸附（四方向）

- `UBuildStructureComponent::CollectSnapCandidates(Slot, Level, Out)`：
  返回**与已占格四邻接的空格**。锚定甲板天然是"已占"，所以初始候选就是甲板四周一圈；
  放下一块后该格进入已占集合、**自动从候选里消失**，同时它的四邻成为新候选。
- `FindNearestSnapCandidate()`：预览在 `MaxSnapCells`（默认 1.5 格）内吸附到最近候选，
  Foundation/Floor 类走这条路径，其它槽位仍按光标格。
- `BuildDebug 1` 用青色框画出当前吸附点。
- `DA_BuildPiece_Raft_Deck`：用 **`SM_Raft` 本体网格**按 `fit_scale_to_cell()` 映射到一格，
  扩展出来的甲板与原木筏同款外观。
