# 木筏建造系统 · P0 实施手册（代码级）

> 设计依据：`doc/Raft-Building-System-Design.md`。本文给**可直接落地的代码**。
> 全部文件位于 `Plugins/GameFeatures/Raft/`（见 `AGENTS.md`，严禁外溢）。
> P0 范围：**不接背包、不做 UI、不写 GAS**，用作弊命令跑通
> “放置 → 复制 → 站上去 → 拆除 → 浮力重算”。

---

## 0. 文件清单与模块依赖

```
Plugins/GameFeatures/Raft/Source/RaftRuntime/
├─ Public/Raft/
│  ├─ RaftGameplayTags.h
│  ├─ RaftGridTypes.h
│  ├─ RaftPieceDefinition.h
│  ├─ RaftPieceCatalog.h
│  ├─ RaftBuildResourceSource.h
│  ├─ RaftBuildComponent.h
│  ├─ RaftStructureVisualComponent.h
│  └─ RaftBuildCheats.h
└─ Private/Raft/  （同名 .cpp）
```

`RaftRuntime.Build.cs`：

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core", "CoreUObject", "Engine", "GameplayTags", "NetCore"
});

PrivateDependencyModuleNames.AddRange(new string[]
{
    "OceanCoreRuntime"
});
```

> P0 不要加 `LyraGame`（背包在 P1 接）、不要加 `GameplayMessageRuntime`（消息在 P2 接）。
> `UCheatManagerExtension` 在 `Engine` 模块里，无需额外依赖。

---

## 1. RaftGameplayTags

**RaftGameplayTags.h**

```cpp
#pragma once
#include "NativeGameplayTags.h"

namespace RaftGameplayTags
{
    RAFTRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BuildFail_BadDefinition);
    RAFTRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BuildFail_Occupied);
    RAFTRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BuildFail_NoSupport);
    RAFTRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BuildFail_NoResource);
    RAFTRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BuildFail_Blocked);
    RAFTRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BuildFail_LimitReached);
    RAFTRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BuildFail_NotFound);
    RAFTRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BuildFail_WouldOrphan);
}
```

**RaftGameplayTags.cpp**

```cpp
#include "Raft/RaftGameplayTags.h"

namespace RaftGameplayTags
{
    UE_DEFINE_GAMEPLAY_TAG(BuildFail_BadDefinition, "Raft.BuildFail.BadDefinition");
    UE_DEFINE_GAMEPLAY_TAG(BuildFail_Occupied,      "Raft.BuildFail.Occupied");
    UE_DEFINE_GAMEPLAY_TAG(BuildFail_NoSupport,     "Raft.BuildFail.NoSupport");
    UE_DEFINE_GAMEPLAY_TAG(BuildFail_NoResource,    "Raft.BuildFail.NoResource");
    UE_DEFINE_GAMEPLAY_TAG(BuildFail_Blocked,       "Raft.BuildFail.Blocked");
    UE_DEFINE_GAMEPLAY_TAG(BuildFail_LimitReached,  "Raft.BuildFail.LimitReached");
    UE_DEFINE_GAMEPLAY_TAG(BuildFail_NotFound,      "Raft.BuildFail.NotFound");
    UE_DEFINE_GAMEPLAY_TAG(BuildFail_WouldOrphan,   "Raft.BuildFail.WouldOrphan");
}
```

建造件 Tag（`Raft.Piece.Floor.Wood` 等）写在资产里，用 `Config/Tags/RaftTags.ini` 声明即可，
不必进 C++。

---

## 2. RaftGridTypes.h（纯头文件，无 cpp）

```cpp
#pragma once
#include "CoreMinimal.h"
#include "RaftGridTypes.generated.h"

UENUM(BlueprintType)
enum class ERaftSlotType : uint8
{
    Foundation,   // 基础格（浮筒层），Level 恒为 0
    Floor,        // 地板，占整格水平面
    Wall,         // 墙，占格子的一条边
    Roof,
    Prop          // 功能件
};

USTRUCT(BlueprintType)
struct FRaftGridCoord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 X = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Y = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Level = 0;

    FRaftGridCoord() = default;
    FRaftGridCoord(int32 InX, int32 InY, int32 InLevel) : X(InX), Y(InY), Level(InLevel) {}

    bool operator==(const FRaftGridCoord& Other) const
    {
        return X == Other.X && Y == Other.Y && Level == Other.Level;
    }

    FString ToString() const { return FString::Printf(TEXT("(%d,%d,L%d)"), X, Y, Level); }

    friend uint32 GetTypeHash(const FRaftGridCoord& C)
    {
        return HashCombine(HashCombine(::GetTypeHash(C.X), ::GetTypeHash(C.Y)), ::GetTypeHash(C.Level));
    }
};

USTRUCT(BlueprintType)
struct FRaftSlotKey
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) FRaftGridCoord Coord;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ERaftSlotType Slot = ERaftSlotType::Floor;
    /** 仅 Wall 使用：0=+X 1=+Y 2=-X 3=-Y；其它类型恒为 0 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) uint8 EdgeIndex = 0;

    FRaftSlotKey() = default;
    FRaftSlotKey(const FRaftGridCoord& InCoord, ERaftSlotType InSlot, uint8 InEdge = 0)
        : Coord(InCoord), Slot(InSlot), EdgeIndex(InSlot == ERaftSlotType::Wall ? InEdge : 0) {}

    bool operator==(const FRaftSlotKey& Other) const
    {
        return Coord == Other.Coord && Slot == Other.Slot && EdgeIndex == Other.EdgeIndex;
    }

    friend uint32 GetTypeHash(const FRaftSlotKey& K)
    {
        return HashCombine(HashCombine(GetTypeHash(K.Coord), ::GetTypeHash((uint8)K.Slot)),
                           ::GetTypeHash(K.EdgeIndex));
    }
};

namespace RaftGrid
{
    /** cm，与 SM_Raft 的甲板模块对齐；改这里等于改整套网格 */
    inline constexpr double CellSize    = 200.0;
    inline constexpr double LevelHeight = 250.0;

    inline FRaftGridCoord LocalToCoord(const FVector& Local)
    {
        return FRaftGridCoord(
            FMath::FloorToInt(Local.X / CellSize),
            FMath::FloorToInt(Local.Y / CellSize),
            FMath::FloorToInt(Local.Z / LevelHeight));
    }

    /** 返回格子中心（XY 居中，Z 取该层地面） */
    inline FVector CoordToLocalCenter(const FRaftGridCoord& C)
    {
        return FVector((C.X + 0.5) * CellSize, (C.Y + 0.5) * CellSize, C.Level * LevelHeight);
    }

    /** 墙所在边的中心点（相对格子中心的偏移）与朝向 */
    inline FVector EdgeOffset(uint8 EdgeIndex)
    {
        switch (EdgeIndex & 3)
        {
        case 0:  return FVector( CellSize * 0.5, 0.0, 0.0);
        case 1:  return FVector(0.0,  CellSize * 0.5, 0.0);
        case 2:  return FVector(-CellSize * 0.5, 0.0, 0.0);
        default: return FVector(0.0, -CellSize * 0.5, 0.0);
        }
    }
    inline float EdgeYaw(uint8 EdgeIndex) { return 90.f * (EdgeIndex & 3); }

    /** 同层四邻 + 上下同格，用于支撑与连通性 */
    inline void GetNeighbors(const FRaftGridCoord& C, TArray<FRaftGridCoord>& Out)
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

## 3. 建造件定义与目录

**RaftPieceDefinition.h**

```cpp
#pragma once
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Raft/RaftGridTypes.h"
#include "RaftPieceDefinition.generated.h"

class UStaticMesh;

UCLASS(Abstract, DefaultToInstanced, EditInlineNew)
class RAFTRUNTIME_API URaftPieceFragment : public UObject
{
    GENERATED_BODY()
};

/** 放置规则；缺省即“默认规则”（见 URaftBuildComponent::CheckSupport） */
UCLASS(DisplayName = "Placement Rules")
class RAFTRUNTIME_API URaftPieceFragment_PlacementRules : public URaftPieceFragment
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
class RAFTRUNTIME_API URaftPieceFragment_Collision : public URaftPieceFragment
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly) bool bBlocking = true;
    UPROPERTY(EditDefaultsOnly) bool bCanCharacterStepUpOn = true;
};

UCLASS(BlueprintType, Const)
class RAFTRUNTIME_API URaftPieceDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, Category = "Raft|Piece") FGameplayTag PieceTag;
    UPROPERTY(EditDefaultsOnly, Category = "Raft|Piece") ERaftSlotType SlotType = ERaftSlotType::Floor;
    UPROPERTY(EditDefaultsOnly, Category = "Raft|Piece") TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditDefaultsOnly, Category = "Raft|Piece", meta = (Units = "cm")) FVector MeshOffset = FVector::ZeroVector;
    UPROPERTY(EditDefaultsOnly, Category = "Raft|Piece") FIntPoint Footprint = FIntPoint(1, 1);

    UPROPERTY(EditDefaultsOnly, Instanced, Category = "Raft|Piece") TArray<TObjectPtr<URaftPieceFragment>> Fragments;

    template <typename T>
    const T* FindFragment() const
    {
        for (const URaftPieceFragment* Fragment : Fragments)
        {
            if (const T* Typed = Cast<T>(Fragment)) { return Typed; }
        }
        return nullptr;
    }
};
```

**RaftPieceCatalog.h** —— 复制用索引表，避免直传资产指针

```cpp
#pragma once
#include "Engine/DataAsset.h"
#include "RaftPieceCatalog.generated.h"

class URaftPieceDefinition;
struct FGameplayTag;

UCLASS(BlueprintType, Const)
class RAFTRUNTIME_API URaftPieceCatalog : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** 顺序即网络索引，**只能追加，禁止插入或删除中间项** */
    UPROPERTY(EditDefaultsOnly, Category = "Raft") TArray<TObjectPtr<URaftPieceDefinition>> Pieces;

    const URaftPieceDefinition* GetByIndex(uint16 Index) const
    {
        return Pieces.IsValidIndex(Index) ? Pieces[Index].Get() : nullptr;
    }

    bool FindIndex(const URaftPieceDefinition* Def, uint16& OutIndex) const
    {
        const int32 Index = Pieces.IndexOfByKey(Def);
        if (Index == INDEX_NONE) { return false; }
        OutIndex = static_cast<uint16>(Index);
        return true;
    }

    const URaftPieceDefinition* FindByTag(FGameplayTag Tag) const;   // cpp 里线性查找即可
};
```

---

## 4. URaftBuildComponent —— 结构真值

### 4.1 RaftBuildComponent.h

```cpp
#pragma once
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Raft/RaftGridTypes.h"
#include "RaftBuildComponent.generated.h"

class URaftBuildComponent;
class URaftPieceCatalog;
class URaftPieceDefinition;
class AController;

USTRUCT()
struct FRaftPieceEntry : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY() FRaftSlotKey Key;
    UPROPERTY() uint16 PieceIndex = 0;   // → URaftPieceCatalog
    UPROPERTY() uint8  Rotation   = 0;   // 0..3，90° 步进

    void PostReplicatedAdd(const struct FRaftPieceList& InArray);
    void PostReplicatedRemove(const struct FRaftPieceList& InArray);
    void PostReplicatedChange(const struct FRaftPieceList& InArray);
};

USTRUCT()
struct FRaftPieceList : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY() TArray<FRaftPieceEntry> Entries;
    UPROPERTY(NotReplicated) TObjectPtr<URaftBuildComponent> OwnerComponent = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FRaftPieceEntry, FRaftPieceList>(
            Entries, DeltaParms, *this);
    }
};

template <>
struct TStructOpsTypeTraits<FRaftPieceList> : public TStructOpsTypeTraitsBase2<FRaftPieceList>
{
    enum { WithNetDeltaSerializer = true };
};

DECLARE_MULTICAST_DELEGATE(FOnRaftStructureChanged);

UCLASS(BlueprintType, ClassGroup = (Raft), meta = (BlueprintSpawnableComponent))
class RAFTRUNTIME_API URaftBuildComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    URaftBuildComponent();

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // —— 查询（双端可用）——
    UFUNCTION(BlueprintPure, Category = "Raft|Build")
    bool CanPlacePiece(const FRaftSlotKey& Key, const URaftPieceDefinition* Def, FGameplayTag& OutFailReason) const;

    UFUNCTION(BlueprintPure, Category = "Raft|Build")
    const URaftPieceDefinition* QueryPiece(const FRaftSlotKey& Key) const;

    const TArray<FRaftPieceEntry>& GetEntries() const { return PieceList.Entries; }
    const URaftPieceCatalog* GetCatalog() const { return PieceCatalog; }
    const TSet<FRaftGridCoord>& GetAnchorCells() const { return AnchorCells; }

    /** 世界坐标 → 槽位；bOutHitRaft 表示是否落在本木筏范围内 */
    UFUNCTION(BlueprintPure, Category = "Raft|Build")
    FRaftSlotKey WorldToSlot(const FVector& WorldLocation, ERaftSlotType Slot) const;

    UFUNCTION(BlueprintPure, Category = "Raft|Build")
    FVector SlotToWorld(const FRaftSlotKey& Key) const;

    /** 结构的局部包围盒，供浮力/甲板重算 */
    FBox ComputeLocalStructureBounds() const;

    // —— 唯一写入口（服务端）——
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Raft|Build")
    bool TryPlacePiece(const FRaftSlotKey& Key, const URaftPieceDefinition* Def, uint8 Rotation, AController* Instigator);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Raft|Build")
    bool TryRemovePiece(const FRaftSlotKey& Key, AController* Instigator);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Raft|Build")
    int32 ClearAllPieces();

    /** 结构变化通知（服务端写入后、客户端收到复制后都会触发） */
    FOnRaftStructureChanged OnStructureChanged;

    UPROPERTY(EditDefaultsOnly, Category = "Raft|Build")
    TObjectPtr<const URaftPieceCatalog> PieceCatalog;

    UPROPERTY(EditDefaultsOnly, Category = "Raft|Build", meta = (ClampMin = "1"))
    int32 MaxPieceCount = 500;

protected:
    UPROPERTY(Replicated) FRaftPieceList PieceList;

    /** 本地索引，不复制；任何写入/复制回调后整表重建 */
    TMap<FRaftSlotKey, int32> SlotToEntryIndex;

    /** 基础木筏覆盖的格子，既是支撑起点也是连通性 BFS 种子 */
    TSet<FRaftGridCoord> AnchorCells;

    void RebuildAnchorCells();
    void RebuildIndex();
    bool CheckSupport(const FRaftSlotKey& Key, const URaftPieceDefinition* Def) const;
    bool HasPieceAt(const FRaftGridCoord& Coord, ERaftSlotType Slot) const;
    bool IsCellBlockedByPawn(const FRaftGridCoord& Coord) const;
    bool WouldStayConnectedWithout(int32 SkipEntryIndex) const;
    void NotifyStructureChanged();

    friend struct FRaftPieceEntry;
    void HandleEntriesReplicated();   // 复制回调走这里，合并成一次重建

private:
    bool bRebuildQueued = false;
};
```

### 4.2 RaftBuildComponent.cpp（关键实现）

```cpp
#include "Raft/RaftBuildComponent.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Net/UnrealNetwork.h"
#include "Raft/RaftActor.h"
#include "Raft/RaftGameplayTags.h"
#include "Raft/RaftPieceCatalog.h"
#include "Raft/RaftPieceDefinition.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RaftBuildComponent)

// ---------- FastArray 回调：只做“通知”，不做业务 ----------
void FRaftPieceEntry::PostReplicatedAdd(const FRaftPieceList& InArray)
{
    if (InArray.OwnerComponent) { InArray.OwnerComponent->HandleEntriesReplicated(); }
}
void FRaftPieceEntry::PostReplicatedRemove(const FRaftPieceList& InArray)
{
    if (InArray.OwnerComponent) { InArray.OwnerComponent->HandleEntriesReplicated(); }
}
void FRaftPieceEntry::PostReplicatedChange(const FRaftPieceList& InArray)
{
    if (InArray.OwnerComponent) { InArray.OwnerComponent->HandleEntriesReplicated(); }
}

// ---------- 构造与复制 ----------
URaftBuildComponent::URaftBuildComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void URaftBuildComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(URaftBuildComponent, PieceList);
}

void URaftBuildComponent::BeginPlay()
{
    Super::BeginPlay();

    PieceList.OwnerComponent = this;   // 复制回调靠它找回组件
    RebuildAnchorCells();
    RebuildIndex();
    NotifyStructureChanged();
}
```

**为什么复制回调要延后合并**：一个网络包里可能连续 Add 多条，逐条重建索引与 ISM 是浪费。

```cpp
void URaftBuildComponent::HandleEntriesReplicated()
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

void URaftBuildComponent::RebuildIndex()
{
    SlotToEntryIndex.Reset();
    SlotToEntryIndex.Reserve(PieceList.Entries.Num());
    for (int32 Index = 0; Index < PieceList.Entries.Num(); ++Index)
    {
        SlotToEntryIndex.Add(PieceList.Entries[Index].Key, Index);
    }
}

void URaftBuildComponent::NotifyStructureChanged()
{
    OnStructureChanged.Broadcast();
}
```

**锚定格**：由基础甲板的 Box 尺寸换算出 Level 0 的格集合。

```cpp
void URaftBuildComponent::RebuildAnchorCells()
{
    AnchorCells.Reset();

    const ARaftActor* Raft = Cast<ARaftActor>(GetOwner());
    const UBoxComponent* Deck = Raft ? Raft->GetDeckCollision() : nullptr;
    if (!Deck) { return; }

    const FVector Extent = Deck->GetUnscaledBoxExtent();
    const FRaftGridCoord Min = RaftGrid::LocalToCoord(FVector(-Extent.X, -Extent.Y, 0.0));
    const FRaftGridCoord Max = RaftGrid::LocalToCoord(FVector( Extent.X - KINDA_SMALL_NUMBER,
                                                               Extent.Y - KINDA_SMALL_NUMBER, 0.0));
    for (int32 X = Min.X; X <= Max.X; ++X)
    {
        for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
        {
            AnchorCells.Add(FRaftGridCoord(X, Y, 0));
        }
    }
}
```

**坐标换算**（注意：全部经 Actor 变换，天生跟随海浪姿态）：

```cpp
FRaftSlotKey URaftBuildComponent::WorldToSlot(const FVector& WorldLocation, ERaftSlotType Slot) const
{
    const AActor* Owner = GetOwner();
    const FVector Local = Owner ? Owner->GetActorTransform().InverseTransformPosition(WorldLocation)
                                : WorldLocation;
    FRaftSlotKey Key(RaftGrid::LocalToCoord(Local), Slot);

    if (Slot == ERaftSlotType::Wall)
    {
        // 取离命中点最近的一条边
        const FVector Center = RaftGrid::CoordToLocalCenter(Key.Coord);
        const FVector Delta  = Local - Center;
        Key.EdgeIndex = FMath::Abs(Delta.X) > FMath::Abs(Delta.Y)
            ? (Delta.X > 0 ? 0 : 2)
            : (Delta.Y > 0 ? 1 : 3);
    }
    return Key;
}

FVector URaftBuildComponent::SlotToWorld(const FRaftSlotKey& Key) const
{
    FVector Local = RaftGrid::CoordToLocalCenter(Key.Coord);
    if (Key.Slot == ERaftSlotType::Wall) { Local += RaftGrid::EdgeOffset(Key.EdgeIndex); }

    const AActor* Owner = GetOwner();
    return Owner ? Owner->GetActorTransform().TransformPosition(Local) : Local;
}
```

**放置校验**（唯一规则来源，客户端预判与服务端复检共用同一函数）：

```cpp
bool URaftBuildComponent::CanPlacePiece(const FRaftSlotKey& Key, const URaftPieceDefinition* Def,
                                        FGameplayTag& OutFailReason) const
{
    OutFailReason = FGameplayTag();

    if (!Def || !PieceCatalog) { OutFailReason = RaftGameplayTags::BuildFail_BadDefinition; return false; }
    if (Def->SlotType != Key.Slot) { OutFailReason = RaftGameplayTags::BuildFail_BadDefinition; return false; }

    uint16 Unused = 0;
    if (!PieceCatalog->FindIndex(Def, Unused)) { OutFailReason = RaftGameplayTags::BuildFail_BadDefinition; return false; }

    if (PieceList.Entries.Num() >= MaxPieceCount) { OutFailReason = RaftGameplayTags::BuildFail_LimitReached; return false; }

    // Footprint 覆盖的每一格都要空
    for (int32 DX = 0; DX < FMath::Max(1, Def->Footprint.X); ++DX)
    {
        for (int32 DY = 0; DY < FMath::Max(1, Def->Footprint.Y); ++DY)
        {
            const FRaftSlotKey Sub(FRaftGridCoord(Key.Coord.X + DX, Key.Coord.Y + DY, Key.Coord.Level),
                                   Key.Slot, Key.EdgeIndex);
            if (SlotToEntryIndex.Contains(Sub)) { OutFailReason = RaftGameplayTags::BuildFail_Occupied; return false; }
        }
    }

    if (!CheckSupport(Key, Def)) { OutFailReason = RaftGameplayTags::BuildFail_NoSupport; return false; }

    const URaftPieceFragment_PlacementRules* Rules = Def->FindFragment<URaftPieceFragment_PlacementRules>();
    if ((!Rules || Rules->bBlockedByPawns) && Key.Slot != ERaftSlotType::Floor
        && IsCellBlockedByPawn(Key.Coord))
    {
        OutFailReason = RaftGameplayTags::BuildFail_Blocked;   // 别把玩家封在墙里
        return false;
    }

    return true;
}
```

**支撑规则**（P0 最小可用版，后续加规则只改这一个函数）：

```cpp
bool URaftBuildComponent::CheckSupport(const FRaftSlotKey& Key, const URaftPieceDefinition* Def) const
{
    if (const URaftPieceFragment_PlacementRules* Rules = Def->FindFragment<URaftPieceFragment_PlacementRules>())
    {
        if (Rules->bAllowFloating) { return true; }
    }

    const FRaftGridCoord& C = Key.Coord;

    switch (Key.Slot)
    {
    case ERaftSlotType::Foundation:
    {
        if (C.Level != 0) { return false; }
        // 与基础木筏或已有 Foundation 4-邻接
        const FRaftGridCoord N[4] = {{C.X+1,C.Y,0},{C.X-1,C.Y,0},{C.X,C.Y+1,0},{C.X,C.Y-1,0}};
        for (const FRaftGridCoord& Coord : N)
        {
            if (AnchorCells.Contains(Coord) || HasPieceAt(Coord, ERaftSlotType::Foundation)) { return true; }
        }
        return false;
    }
    case ERaftSlotType::Floor:
    {
        // 同格下方有基础/Foundation/下层地板，或同层 4-邻接已有地板
        if (C.Level == 0 && (AnchorCells.Contains(C) || HasPieceAt(C, ERaftSlotType::Foundation))) { return true; }
        if (C.Level > 0 && HasPieceAt(FRaftGridCoord(C.X, C.Y, C.Level - 1), ERaftSlotType::Floor)) { return true; }

        const FRaftGridCoord N[4] = {{C.X+1,C.Y,C.Level},{C.X-1,C.Y,C.Level},
                                     {C.X,C.Y+1,C.Level},{C.X,C.Y-1,C.Level}};
        for (const FRaftGridCoord& Coord : N)
        {
            if (HasPieceAt(Coord, ERaftSlotType::Floor)) { return true; }
        }
        return false;
    }
    case ERaftSlotType::Wall:
    case ERaftSlotType::Roof:
    case ERaftSlotType::Prop:
    default:
        // 依附于本格地板（或基础甲板）
        return HasPieceAt(C, ERaftSlotType::Floor) || (C.Level == 0 && AnchorCells.Contains(C));
    }
}

bool URaftBuildComponent::HasPieceAt(const FRaftGridCoord& Coord, ERaftSlotType Slot) const
{
    return SlotToEntryIndex.Contains(FRaftSlotKey(Coord, Slot));
}

bool URaftBuildComponent::IsCellBlockedByPawn(const FRaftGridCoord& Coord) const
{
    const UWorld* World = GetWorld();
    const AActor* Owner = GetOwner();
    if (!World || !Owner) { return false; }

    const FVector Center = Owner->GetActorTransform().TransformPosition(
        RaftGrid::CoordToLocalCenter(Coord) + FVector(0.0, 0.0, RaftGrid::LevelHeight * 0.5));
    const FCollisionShape Box = FCollisionShape::MakeBox(
        FVector(RaftGrid::CellSize * 0.5, RaftGrid::CellSize * 0.5, RaftGrid::LevelHeight * 0.5));

    FCollisionQueryParams Params(SCENE_QUERY_STAT(RaftBuildBlocked), false, Owner);
    return World->OverlapAnyTestByChannel(Center, Owner->GetActorQuat(), ECC_Pawn, Box, Params);
}
```

**放置**：

```cpp
bool URaftBuildComponent::TryPlacePiece(const FRaftSlotKey& Key, const URaftPieceDefinition* Def,
                                        uint8 Rotation, AController* Instigator)
{
    AActor* Owner = GetOwner();
    if (!Owner || !Owner->HasAuthority()) { return false; }   // 服务端权威，硬拦

    FGameplayTag FailReason;
    if (!CanPlacePiece(Key, Def, FailReason))
    {
        UE_LOG(LogTemp, Verbose, TEXT("[Raft] Place %s rejected: %s"),
               *Key.Coord.ToString(), *FailReason.ToString());
        return false;
    }

    uint16 PieceIndex = 0;
    PieceCatalog->FindIndex(Def, PieceIndex);

    FRaftPieceEntry& NewEntry = PieceList.Entries.AddDefaulted_GetRef();
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
bool URaftBuildComponent::TryRemovePiece(const FRaftSlotKey& Key, AController* Instigator)
{
    AActor* Owner = GetOwner();
    if (!Owner || !Owner->HasAuthority()) { return false; }

    const int32* IndexPtr = SlotToEntryIndex.Find(Key);
    if (!IndexPtr) { return false; }                       // BuildFail_NotFound

    const FRaftPieceEntry& Entry = PieceList.Entries[*IndexPtr];
    if (const URaftPieceDefinition* Def = PieceCatalog ? PieceCatalog->GetByIndex(Entry.PieceIndex) : nullptr)
    {
        if (const URaftPieceFragment_PlacementRules* Rules = Def->FindFragment<URaftPieceFragment_PlacementRules>())
        {
            if (!Rules->bRemovable) { return false; }
        }
    }

    if (!WouldStayConnectedWithout(*IndexPtr)) { return false; }   // BuildFail_WouldOrphan

    PieceList.Entries.RemoveAtSwap(*IndexPtr);
    PieceList.MarkArrayDirty();             // ← 删除必须用 MarkArrayDirty，不是 MarkItemDirty

    RebuildIndex();
    Owner->ForceNetUpdate();
    NotifyStructureChanged();
    return true;
}

bool URaftBuildComponent::WouldStayConnectedWithout(int32 SkipEntryIndex) const
{
    // 1) 收集移除后剩余的“承重格”
    TSet<FRaftGridCoord> SupportCells;
    for (int32 i = 0; i < PieceList.Entries.Num(); ++i)
    {
        if (i == SkipEntryIndex) { continue; }
        const FRaftSlotKey& K = PieceList.Entries[i].Key;
        if (K.Slot == ERaftSlotType::Foundation || K.Slot == ERaftSlotType::Floor)
        {
            SupportCells.Add(K.Coord);
        }
    }

    // 2) 从锚定格 BFS
    TSet<FRaftGridCoord> Visited;
    TArray<FRaftGridCoord> Queue;
    for (const FRaftGridCoord& Anchor : AnchorCells)
    {
        Visited.Add(Anchor);
        Queue.Add(Anchor);
    }

    TArray<FRaftGridCoord> Neighbors;
    while (Queue.Num() > 0)
    {
        const FRaftGridCoord Current = Queue.Pop(EAllowShrinking::No);
        RaftGrid::GetNeighbors(Current, Neighbors);
        for (const FRaftGridCoord& N : Neighbors)
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
        const FRaftGridCoord& C = PieceList.Entries[i].Key.Coord;
        if (!Visited.Contains(C) && !AnchorCells.Contains(C)) { return false; }
    }
    return true;
}
```

**清空与包围盒**：

```cpp
int32 URaftBuildComponent::ClearAllPieces()
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

FBox URaftBuildComponent::ComputeLocalStructureBounds() const
{
    FBox Bounds(ForceInit);
    for (const FRaftGridCoord& Anchor : AnchorCells)
    {
        const FVector Center = RaftGrid::CoordToLocalCenter(Anchor);
        Bounds += FBox::BuildAABB(Center, FVector(RaftGrid::CellSize * 0.5, RaftGrid::CellSize * 0.5, 1.0));
    }
    for (const FRaftPieceEntry& Entry : PieceList.Entries)
    {
        const FVector Center = RaftGrid::CoordToLocalCenter(Entry.Key.Coord);
        Bounds += FBox::BuildAABB(Center, FVector(RaftGrid::CellSize * 0.5, RaftGrid::CellSize * 0.5, 1.0));
    }
    return Bounds;
}
```

---

## 5. URaftStructureVisualComponent —— ISM 表现与碰撞

**RaftStructureVisualComponent.h**

```cpp
#pragma once
#include "Components/ActorComponent.h"
#include "RaftStructureVisualComponent.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class URaftBuildComponent;

UCLASS(BlueprintType, ClassGroup = (Raft), meta = (BlueprintSpawnableComponent))
class RAFTRUNTIME_API URaftStructureVisualComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    URaftStructureVisualComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

protected:
    void RebuildInstances();
    UInstancedStaticMeshComponent* FindOrCreateISM(UStaticMesh* Mesh);

    UPROPERTY(Transient) TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UInstancedStaticMeshComponent>> MeshToISM;
    TWeakObjectPtr<URaftBuildComponent> BuildComponent;
};
```

**RaftStructureVisualComponent.cpp**

```cpp
void URaftStructureVisualComponent::BeginPlay()
{
    Super::BeginPlay();

    if (URaftBuildComponent* Build = GetOwner() ? GetOwner()->FindComponentByClass<URaftBuildComponent>() : nullptr)
    {
        BuildComponent = Build;
        Build->OnStructureChanged.AddUObject(this, &ThisClass::RebuildInstances);
        RebuildInstances();      // 后加入的客户端首帧就能看到已有结构
    }
}

UInstancedStaticMeshComponent* URaftStructureVisualComponent::FindOrCreateISM(UStaticMesh* Mesh)
{
    if (TObjectPtr<UInstancedStaticMeshComponent>* Found = MeshToISM.Find(Mesh))
    {
        return *Found;
    }

    ARaftActor* Raft = Cast<ARaftActor>(GetOwner());
    USceneComponent* AttachTo = Raft ? Cast<USceneComponent>(Raft->GetDeckCollision()) : nullptr;
    if (!AttachTo) { return nullptr; }

    UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(GetOwner());
    ISM->SetStaticMesh(Mesh);
    ISM->SetMobility(EComponentMobility::Movable);
    ISM->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
    ISM->CanCharacterStepUpOn = ECB_Yes;             // 角色能站上去
    ISM->SetGenerateOverlapEvents(false);
    ISM->SetCanEverAffectNavigation(false);
    ISM->SetupAttachment(AttachTo);                  // ← 必须挂 DeckCollision，与 MovementBase 同源
    ISM->RegisterComponent();
    ISM->SetRelativeTransform(FTransform::Identity); // 实例变换 == 木筏局部坐标

    MeshToISM.Add(Mesh, ISM);
    return ISM;
}

void URaftStructureVisualComponent::RebuildInstances()
{
    URaftBuildComponent* Build = BuildComponent.Get();
    if (!Build) { return; }

    for (TPair<TObjectPtr<UStaticMesh>, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : MeshToISM)
    {
        Pair.Value->ClearInstances();     // P0 整表重建：最简单可靠；P2 再改增量
    }

    const URaftPieceCatalog* Catalog = Build->GetCatalog();
    if (!Catalog) { return; }

    for (const FRaftPieceEntry& Entry : Build->GetEntries())
    {
        const URaftPieceDefinition* Def = Catalog->GetByIndex(Entry.PieceIndex);
        if (!Def || !Def->Mesh) { continue; }

        UInstancedStaticMeshComponent* ISM = FindOrCreateISM(Def->Mesh);
        if (!ISM) { continue; }

        FVector Location = RaftGrid::CoordToLocalCenter(Entry.Key.Coord) + Def->MeshOffset;
        float Yaw = 90.f * Entry.Rotation;
        if (Entry.Key.Slot == ERaftSlotType::Wall)
        {
            Location += RaftGrid::EdgeOffset(Entry.Key.EdgeIndex);
            Yaw = RaftGrid::EdgeYaw(Entry.Key.EdgeIndex);
        }

        ISM->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), Location), /*bWorldSpace=*/false);
    }
}
```

---

## 6. 资源来源接口（背包解耦点）

**RaftBuildResourceSource.h**

```cpp
#pragma once
#include "UObject/Interface.h"
#include "RaftBuildResourceSource.generated.h"

USTRUCT(BlueprintType)
struct FRaftBuildCost
{
    GENERATED_BODY()
    /** P1 接背包时改成 TSubclassOf<ULyraInventoryItemDefinition> */
    UPROPERTY(EditDefaultsOnly) TSoftClassPtr<UObject> ItemDefinition;
    UPROPERTY(EditDefaultsOnly, meta = (ClampMin = "1")) int32 Count = 1;
};

UINTERFACE(MinimalAPI, NotBlueprintable)
class URaftBuildResourceSource : public UInterface { GENERATED_BODY() };

class IRaftBuildResourceSource
{
    GENERATED_BODY()
public:
    virtual bool HasResources(const TArray<FRaftBuildCost>& Costs) const = 0;
    virtual bool ConsumeResources(const TArray<FRaftBuildCost>& Costs) = 0;
    virtual void RefundResources(const TArray<FRaftBuildCost>& Costs) = 0;
};

/** P0：无限材料 */
UCLASS()
class RAFTRUNTIME_API URaftCreativeResourceSource : public UObject, public IRaftBuildResourceSource
{
    GENERATED_BODY()
public:
    virtual bool HasResources(const TArray<FRaftBuildCost>&) const override { return true; }
    virtual bool ConsumeResources(const TArray<FRaftBuildCost>&) override { return true; }
    virtual void RefundResources(const TArray<FRaftBuildCost>&) override {}
};
```

P1 只需新增 `URaftInventoryResourceSource`（内部持有
`ULyraInventoryManagerComponent*`，转发 `ConsumeItemsByDefinition` / `AddItemDefinition`），
在 `URaftBuildComponent` 里换一个 `TScriptInterface` 赋值，**建造逻辑一行不改**。

---

## 7. 建造 GM：URaftBuildCheats

**RaftBuildCheats.h**

```cpp
#pragma once
#include "GameFramework/CheatManager.h"
#include "Raft/RaftGridTypes.h"
#include "RaftBuildCheats.generated.h"

class ARaftActor;
class URaftBuildComponent;
class URaftPieceDefinition;

UCLASS(NotBlueprintable)
class URaftBuildCheats final : public UCheatManagerExtension
{
    GENERATED_BODY()

public:
    URaftBuildCheats();

    UFUNCTION(Exec) void RaftBuildSelect(const FString& PieceTag);
    UFUNCTION(Exec, BlueprintAuthorityOnly) void RaftBuildPlace(int32 X = MIN_int32, int32 Y = 0, int32 Level = 0);
    UFUNCTION(Exec, BlueprintAuthorityOnly) void RaftBuildRemove(int32 X = MIN_int32, int32 Y = 0, int32 Level = 0);
    UFUNCTION(Exec, BlueprintAuthorityOnly) void RaftBuildFill(int32 SizeX, int32 SizeY);
    UFUNCTION(Exec, BlueprintAuthorityOnly) void RaftBuildClear();
    UFUNCTION(Exec) void RaftBuildDump();
    UFUNCTION(Exec) void RaftBuildDebug(int32 bEnabled);

private:
    URaftBuildComponent* GetBuildComponent() const;
    const URaftPieceDefinition* GetSelectedPiece() const;
    bool GetCursorSlot(FRaftSlotKey& OutKey) const;

    FString SelectedPieceTag = TEXT("Raft.Piece.Floor.Wood");
};
```

**RaftBuildCheats.cpp**（注册方式与 `ULyraBotCheats` 完全一致）

```cpp
#include "Raft/RaftBuildCheats.h"

#include "GameFramework/CheatManagerDefines.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Raft/RaftActor.h"
#include "Raft/RaftBuildComponent.h"
#include "Raft/RaftPieceCatalog.h"
#include "Raft/RaftPieceDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RaftBuildCheats)

URaftBuildCheats::URaftBuildCheats()
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

URaftBuildComponent* URaftBuildCheats::GetBuildComponent() const
{
#if UE_WITH_CHEAT_MANAGER
    APlayerController* PC = GetPlayerController();
    if (!PC) { return nullptr; }

    // 优先取角色脚下的木筏（MovementBase），否则取世界里第一个
    if (const APawn* Pawn = PC->GetPawn())
    {
        if (const UPrimitiveComponent* Base = Pawn->GetMovementBase())
        {
            if (AActor* BaseOwner = Base->GetOwner())
            {
                if (URaftBuildComponent* Build = BaseOwner->FindComponentByClass<URaftBuildComponent>())
                {
                    return Build;
                }
            }
        }
    }
    if (AActor* Raft = UGameplayStatics::GetActorOfClass(PC->GetWorld(), ARaftActor::StaticClass()))
    {
        return Raft->FindComponentByClass<URaftBuildComponent>();
    }
#endif
    return nullptr;
}

bool URaftBuildCheats::GetCursorSlot(FRaftSlotKey& OutKey) const
{
#if UE_WITH_CHEAT_MANAGER
    APlayerController* PC = GetPlayerController();
    URaftBuildComponent* Build = GetBuildComponent();
    if (!PC || !Build) { return false; }

    FHitResult Hit;
    // TopDown 用光标；第一人称/第三人称改成相机前向 LineTrace
    if (PC->GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), false, Hit)
        && Hit.bBlockingHit)
    {
        const URaftPieceDefinition* Def = GetSelectedPiece();
        OutKey = Build->WorldToSlot(Hit.ImpactPoint, Def ? Def->SlotType : ERaftSlotType::Floor);
        return true;
    }
#endif
    return false;
}

void URaftBuildCheats::RaftBuildPlace(int32 X, int32 Y, int32 Level)
{
#if UE_WITH_CHEAT_MANAGER
    URaftBuildComponent* Build = GetBuildComponent();
    const URaftPieceDefinition* Def = GetSelectedPiece();
    if (!Build || !Def) { return; }

    FRaftSlotKey Key;
    if (X == MIN_int32)                       // 未传坐标 → 用准星
    {
        if (!GetCursorSlot(Key)) { return; }
    }
    else
    {
        Key = FRaftSlotKey(FRaftGridCoord(X, Y, Level), Def->SlotType);
    }

    Build->TryPlacePiece(Key, Def, /*Rotation=*/0, GetPlayerController());
#endif
}

void URaftBuildCheats::RaftBuildFill(int32 SizeX, int32 SizeY)
{
#if UE_WITH_CHEAT_MANAGER
    URaftBuildComponent* Build = GetBuildComponent();
    const URaftPieceDefinition* Def = GetSelectedPiece();
    if (!Build || !Def) { return; }

    int32 Placed = 0;
    for (int32 X = 0; X < SizeX; ++X)
    {
        for (int32 Y = 0; Y < SizeY; ++Y)
        {
            // 从锚定区外沿逐圈扩散，保证每一步都有支撑
            if (Build->TryPlacePiece(FRaftSlotKey(FRaftGridCoord(X, Y, 0), Def->SlotType), Def, 0,
                                     GetPlayerController()))
            {
                ++Placed;
            }
        }
    }
    UE_LOG(LogTemp, Log, TEXT("[Raft] Fill placed %d pieces"), Placed);
#endif
}

void URaftBuildCheats::RaftBuildDump()
{
#if UE_WITH_CHEAT_MANAGER
    const URaftBuildComponent* Build = GetBuildComponent();
    if (!Build) { return; }

    UE_LOG(LogTemp, Log, TEXT("[Raft] %d anchors, %d pieces"),
           Build->GetAnchorCells().Num(), Build->GetEntries().Num());
    for (const FRaftPieceEntry& Entry : Build->GetEntries())
    {
        const URaftPieceDefinition* Def = Build->GetCatalog()->GetByIndex(Entry.PieceIndex);
        UE_LOG(LogTemp, Log, TEXT("  %s slot=%d edge=%d rot=%d def=%s"),
               *Entry.Key.Coord.ToString(), (int32)Entry.Key.Slot, Entry.Key.EdgeIndex, Entry.Rotation,
               Def ? *Def->PieceTag.ToString() : TEXT("<null>"));
    }
#endif
}
```

> `RaftBuildFill` 按 X 外层、Y 内层扫描时，第一列之外的格子可能一时无支撑而被拒。
> 简单做法是循环两遍，或按“到锚定区曼哈顿距离”排序后再放；调试命令不必追求最优。

`RaftBuildDebug` 在 `URaftBuildComponent` 里配一个 `bDrawDebug` + `DrawDebugBox/DrawDebugString`，
在 `TickComponent`（调试时才开 tick）或 `UDebugDrawService` 回调里画：
锚定格白框、已占格绿框、当前准星格黄框、结构包围盒红框。

---

## 8. 浮力联动（改 `URaftBuoyancyComponent`）

新增：

```cpp
UFUNCTION(BlueprintAuthorityOnly, Category = "Raft|Buoyancy")
void RebuildFromStructure(const FBox& LocalStructureBounds);
```

```cpp
void URaftBuoyancyComponent::RebuildFromStructure(const FBox& LocalBounds)
{
    if (!GetOwner()->HasAuthority() || !LocalBounds.IsValid) { return; }

    const FVector Extent = LocalBounds.GetExtent();
    PontoonOffsets.Reset(4);
    const double Inset = 0.85;   // 稍微内缩，避免采样点跑到结构外
    PontoonOffsets.Add(FVector( Extent.X * Inset,  Extent.Y * Inset, 0.0));
    PontoonOffsets.Add(FVector( Extent.X * Inset, -Extent.Y * Inset, 0.0));
    PontoonOffsets.Add(FVector(-Extent.X * Inset,  Extent.Y * Inset, 0.0));
    PontoonOffsets.Add(FVector(-Extent.X * Inset, -Extent.Y * Inset, 0.0));
}
```

服务端在 `ARaftActor` 里接线：

```cpp
// ARaftActor::BeginPlay()
if (HasAuthority() && BuildComponent)
{
    BuildComponent->OnStructureChanged.AddUObject(this, &ARaftActor::HandleStructureChanged);
}

void ARaftActor::HandleStructureChanged()
{
    const FBox Local = BuildComponent->ComputeLocalStructureBounds();
    BuoyancyComponent->RebuildFromStructure(Local);

    // 甲板碰撞跟着长大：BoxComponent 以自身原点为中心，
    // 结构不对称时不要动根组件位置，改用一个挂在根下的 DeckExtension Box 承载偏移。
    DeckCollision->SetBoxExtent(FVector(Local.GetExtent().X, Local.GetExtent().Y,
                                        DeckCollision->GetUnscaledBoxExtent().Z));
}
```

> **重要**：`DeckCollision` 是根组件。若结构向一侧扩展，正确做法是保持根组件位置不变，
> 把偏移量补到 ISM 与浮筒采样点上；直接移动根组件会让整条木筏“跳位”，
> 且客户端会看到一次瞬移。

---

## 9. 资产与 Experience（沿用现有 Python 约定）

`Plugins/GameFeatures/Raft/Content/Python/` 下新增（保持幂等风格）：

- `CreateRaftPieceAssets.py`：`DA_RaftPiece_Foundation_Wood` / `DA_RaftPiece_Floor_Wood` /
  `DA_RaftPiece_Wall_Wood` + `DA_RaftPieceCatalog`（P0 网格件可先用 `SM_Raft` 缩放代替美术资产）。
- `CreateRaftBuildTestExperience.py`：`BP_Experience_RaftBuild_Test`，
  `GameFeaturesToEnable = [OceanAdventure, Raft]`，PawnData 复用 `DA_OceanAdventure_PawnData`。
- 在 Raft 的 `GameFeatureData` 追加 `AddComponents`：
  `ARaftActor ← URaftBuildComponent`、`ARaftActor ← URaftStructureVisualComponent`。
- `ValidateRaftFeature.py` 追加：组件已注入、Catalog 非空、每个 Definition 的 Mesh 非空。

地图直接用现成的 `L_OceanChunkTest`（已放置 “Raft Test Actor”）。

---

## 10. 验证清单（P0 判定标准）

Dedicated Server + 2 客户端：

1. `RaftBuildSelect Raft.Piece.Floor.Wood` → `RaftBuildPlace` → 三端同格出现同一件。
2. `Net PktLag=200`：新增一件只发增量，不是整表。
3. 角色站上新建地板，木筏随浪起伏 —— 不抖动、不下陷（MovementBase 正确）。
4. 拆掉脚下地板 → 角色正常下落，无残留碰撞。
5. `RaftBuildFill 10 10` → ISM 实例数正确，浮筒重算后浮力不跳变。
6. 后加入的客户端拿到完整结构（首次全量复制 + `BeginPlay` 里的 `RebuildInstances`）。
7. 造成孤岛的拆除被拒绝，`RaftBuildDump` 中所有条目都可从锚定格到达。

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
| 拆除后索引错乱 | `RemoveAtSwap` 会打乱下标，必须 `RebuildIndex()` 整表重建 |

---

## 12. 建议提交顺序（每步一个 commit）

1. `RaftGameplayTags` + `RaftGridTypes.h` + Debug 绘制 → 能看到网格与格子编号
2. `URaftPieceDefinition` + `URaftPieceCatalog` + 三个资产 → 编辑器里可配
3. `URaftBuildComponent`（FastArray + 规则 + 连通性）→ `RaftBuildDump` 打得出结构
4. `URaftStructureVisualComponent` → 看得见、走得上去
5. `URaftBuildCheats` + 测试 Experience → 联机跑第 10 节清单
6. 浮力 `RebuildFromStructure` + 甲板包围盒 → 大平台不跳变

第 3 步到第 4 步之间务必先用 `RaftBuildDump` 确认数据层正确再调表现层：
绝大多数“看起来是渲染问题”的 bug，根子都在复制层。
