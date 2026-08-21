# OceanCore 插件迁移方案

> 目标：把当前 `OceanAdventureRuntime` 模块重构为独立的**普通插件** `OceanCore`，与 Lyra 解耦，同时通过 Experience 机制完成组件注入。
>
> 生成日期：2026-08-18
> 适用引擎：UE 5.7 + Lyra

---

## 目录

- [一、方案总览](#一方案总览)
- [二、目标目录结构](#二目标目录结构)
- [三、配置文件](#三配置文件)
- [四、代码改造](#四代码改造)
  - [4.1 Manager：Actor → GameState 组件](#41-manageractor--gamestate-组件)
  - [4.2 Invoker：SceneComponent → ActorComponent](#42-invokerscenecomponent--actorcomponent)
  - [4.3 ChunkActor 调整](#43-chunkactor-调整)
  - [4.4 顺带修复的已知 Bug](#44-顺带修复的已知-bug)
- [五、接线：Experience 配置](#五接线experience-配置)
- [六、地图接入](#六地图接入)
- [七、迁移步骤清单](#七迁移步骤清单)
- [八、验证方法](#八验证方法)
- [九、未来升级路径](#九未来升级路径)

---

## 一、方案总览

### 核心决策

| 决策项 | 选择 | 理由 |
|---|---|---|
| 插件类型 | **普通插件**（非 GameFeature） | 模块是算法库，需保持通用可移植 |
| 是否依赖 `LyraGame` | **否** | 保持零 Lyra 耦合，便于复用 |
| Manager 形态 | **GameState 组件** | 天然单例；可被 Experience 注入；保留复制能力 |
| Invoker 形态 | **UActorComponent** | 避免 attach 陷阱；注入更简单 |
| 注入方式 | **Lyra Experience 的 `AddComponents` Action** | 无需自己变成 GameFeature 即可享受注入机制 |

### 关键原理

`GameFeatureAction_AddComponents` 只需要一个 `TSubclassOf<UActorComponent>`。**这个类完全可以来自普通插件**——Action 本身在 Lyra 那边，我们的插件不需要是 GameFeature。

依赖方向规则（单向）：

```
✅ GameFeature 插件  ──依赖──>  普通插件
❌ 普通插件  ──依赖──>  GameFeature 插件     （不允许）
```

把核心代码放在普通插件里，灵活性最大。

---

## 二、目标目录结构

```
Plugins/
└── OceanCore/
    ├── OceanCore.uplugin
    ├── Resources/
    │   └── Icon128.png
    ├── Config/
    │   └── FilterPlugin.ini
    ├── Content/                                    (可选，放通用材质/网格)
    └── Source/
        └── OceanCoreRuntime/
            ├── OceanCoreRuntime.Build.cs
            ├── Public/
            │   ├── OceanCoreRuntimeModule.h
            │   ├── World/
            │   │   ├── OceanWorldManagerComponent.h
            │   │   ├── OceanChunkActor.h
            │   │   └── OceanChunkInvokerComponent.h
            │   └── Generation/
            │       └── OceanHeightField.h          (后续地形生成)
            └── Private/
                ├── OceanCoreRuntimeModule.cpp
                ├── World/
                │   ├── OceanWorldManagerComponent.cpp
                │   ├── OceanChunkActor.cpp
                │   └── OceanChunkInvokerComponent.cpp
                └── Generation/
                    └── OceanHeightField.cpp
```

### 命名变更对照

| 原名称 | 新名称 |
|---|---|
| `OceanAdventureRuntime`（模块） | `OceanCoreRuntime` |
| `OCEANADVENTURERUNTIME_API` | `OCEANCORERUNTIME_API` |
| `FOceanAdventureRuntimeModule` | `FOceanCoreRuntimeModule` |
| `LogOceanAdventure` | `LogOceanCore` |
| `AOceanWorldManager` | `UOceanWorldManagerComponent` |

> 若不想改名，保留 `OceanAdventure` 命名也完全可以，只需保证插件不是 GameFeature 即可。下文以 `OceanCore` 为例。

---

## 三、配置文件

### 3.1 `OceanCore.uplugin`

```json
{
    "FileVersion": 3,
    "Version": 1,
    "VersionName": "1.0",
    "FriendlyName": "Ocean Core",
    "Description": "Procedural ocean & island terrain generation with chunk streaming.",
    "Category": "World Creation",
    "CreatedBy": "",
    "CreatedByURL": "",
    "DocsURL": "",
    "MarketplaceURL": "",
    "SupportURL": "",
    "CanContainContent": true,
    "IsBetaVersion": false,
    "IsExperimentalVersion": false,
    "Installed": false,
    "Modules": [
        {
            "Name": "OceanCoreRuntime",
            "Type": "Runtime",
            "LoadingPhase": "Default"
        }
    ],
    "Plugins": [
        {
            "Name": "ModularGameplay",
            "Enabled": true
        }
    ]
}
```

**与 GameFeature 版本的关键差异：**

| 字段 | 普通插件 | GameFeature |
|---|---|---|
| `ExplicitlyLoaded` | ❌ 不写 | ✅ 必须 `true` |
| `BuiltInInitialFeatureState` | ❌ 不写 | ✅ `"Registered"` |
| 同名 `GameFeatureData` 资产 | ❌ 不需要 | ✅ 必须有 |
| `Category` | 任意 | 建议 `"Game Features"` |

### 3.2 `OceanCoreRuntime.Build.cs`

```csharp
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OceanCoreRuntime : ModuleRules
{
    public OceanCoreRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "NetCore",              // 复制系统
                "ModularGameplay",      // GameFrameworkComponentManager
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "GeometryCore",         // 后续地形网格生成
                "GeometryFramework",
            }
        );

        // 注意：刻意不依赖 LyraGame，保持模块通用可移植
    }
}
```

> ⚠️ **不要加 `GameFeatures` 依赖**，也**不要加 `LyraGame`**。这两个是保持解耦的关键。

### 3.3 `Config/FilterPlugin.ini`（可选，打包用）

```ini
[FilterPlugin]
; 打包插件时需要额外包含的文件
```

---

## 四、代码改造

### 4.1 Manager：Actor → GameState 组件

#### 头文件 `OceanWorldManagerComponent.h`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"

#include "OceanWorldManagerComponent.generated.h"

class AOceanChunkActor;
class UOceanChunkInvokerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOceanChunkLifecycleSignature, AOceanChunkActor*, Chunk);

/**
 * Server-authoritative owner of the active ocean chunk set for one UWorld.
 * Designed to be added to the GameState (via Lyra Experience or manually).
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Ocean),
       meta = (BlueprintSpawnableComponent))
class OCEANCORERUNTIME_API UOceanWorldManagerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UOceanWorldManagerComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** 便捷静态查找：从任意 WorldContext 拿到 GameState 上的 Manager */
    UFUNCTION(BlueprintPure, Category = "Ocean|Chunk",
              meta = (WorldContext = "WorldContextObject"))
    static UOceanWorldManagerComponent* Get(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ocean|Chunk")
    void RegisterInvoker(UOceanChunkInvokerComponent* Invoker);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ocean|Chunk")
    void UnregisterInvoker(UOceanChunkInvokerComponent* Invoker);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ocean|Chunk")
    void RequestRefreshChunks();

    UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
    FIntPoint GetChunkCoordFromWorldLocation(FVector WorldLocation) const;

    UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
    FVector GetChunkWorldOrigin(FIntPoint ChunkCoord) const;

    UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
    AOceanChunkActor* GetChunkAtCoord(FIntPoint ChunkCoord) const;

    UFUNCTION(BlueprintPure, BlueprintAuthorityOnly, Category = "Ocean|Chunk")
    int32 GetActiveChunkCount() const;

    UFUNCTION(BlueprintPure, BlueprintAuthorityOnly, Category = "Ocean|Chunk")
    TArray<FIntPoint> GetActiveChunkCoords() const;

    UFUNCTION(BlueprintPure, BlueprintAuthorityOnly, Category = "Ocean|Chunk")
    int32 GetRegisteredInvokerCount() const;

    UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
    float GetChunkSize() const { return GetSafeChunkSize(); }

    UFUNCTION(BlueprintPure, Category = "Ocean|Generation")
    int32 GetWorldSeed() const { return WorldSeed; }

    UPROPERTY(BlueprintAssignable, Category = "Ocean|Chunk")
    FOceanChunkLifecycleSignature OnChunkActivated;

    UPROPERTY(BlueprintAssignable, Category = "Ocean|Chunk")
    FOceanChunkLifecycleSignature OnChunkDeactivated;

protected:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
    TSubclassOf<AOceanChunkActor> ChunkClass;

    UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Ocean|Generation")
    int32 WorldSeed = 12345;

    UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Ocean|Chunk",
              meta = (ClampMin = "100.0"))
    float ChunkSize = 20000.0f;

    UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Ocean|Chunk")
    float ChunkBaseZ = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk",
              meta = (ClampMin = "0.05"))
    float RefreshInterval = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk",
              meta = (ClampMin = "0.0"))
    float UnloadGraceSeconds = 10.0f;

private:
    /** 统一的尺寸取值入口，避免钳制不一致 */
    float GetSafeChunkSize() const { return FMath::Max(100.0f, ChunkSize); }

    void RefreshChunks();
    void CleanupInvalidReferences();
    TSet<FIntPoint> BuildRequiredChunkSet() const;
    AOceanChunkActor* SpawnChunk(FIntPoint ChunkCoord);
    void DestroyChunk(FIntPoint ChunkCoord);

    TArray<TWeakObjectPtr<UOceanChunkInvokerComponent>> Invokers;
    TMap<FIntPoint, TWeakObjectPtr<AOceanChunkActor>> ActiveChunks;
    TMap<FIntPoint, double> PendingUnloadDeadlines;
    FTimerHandle RefreshTimer;
    bool bRefreshInProgress = false;
    bool bRefreshRequested = false;      // 合并刷新请求
    bool bLoggedMissingChunkClass = false; // 防日志刷屏
};
```

#### 实现要点 `OceanWorldManagerComponent.cpp`

```cpp
UOceanWorldManagerComponent::UOceanWorldManagerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);      // 关键：组件必须显式开启复制

    ChunkClass = AOceanChunkActor::StaticClass();
}

UOceanWorldManagerComponent* UOceanWorldManagerComponent::Get(const UObject* WorldContextObject)
{
    if (const UWorld* World = GEngine->GetWorldFromContextObject(
            WorldContextObject, EGetWorldErrorMode::ReturnNull))
    {
        if (AGameStateBase* GameState = World->GetGameState())
        {
            return GameState->FindComponentByClass<UOceanWorldManagerComponent>();
        }
    }
    return nullptr;
}

void UOceanWorldManagerComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld())
    {
        return;
    }

    // 启动时校验一次，避免 SpawnChunk 里反复报错
    if (!ChunkClass)
    {
        UE_LOG(LogOceanCore, Error,
            TEXT("%s: ChunkClass is not configured. Ocean chunks will not spawn."),
            *GetNameSafe(GetOwner()));
        return;
    }

    ChunkSize = FMath::Max(100.0f, ChunkSize);
    RefreshInterval = FMath::Max(0.05f, RefreshInterval);
    UnloadGraceSeconds = FMath::Max(0.0f, UnloadGraceSeconds);

    GetWorld()->GetTimerManager().SetTimer(RefreshTimer, this,
        &UOceanWorldManagerComponent::RefreshChunks, RefreshInterval, true);
    RefreshChunks();
}

void UOceanWorldManagerComponent::RequestRefreshChunks()
{
    // 合并请求，交给已有的 RefreshTimer 统一处理
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        bRefreshRequested = true;
    }
}
```

**`HasAuthority()` 替换说明**：`UActorComponent` 没有 `HasAuthority()`，需要改用：

```cpp
// 原写法
if (!HasAuthority()) { ... }

// 新写法（二选一）
if (!GetOwner() || !GetOwner()->HasAuthority()) { ... }
// 或
if (GetOwnerRole() != ROLE_Authority) { ... }
```

**其余方法迁移规则：**

| 原代码 | 新代码 |
|---|---|
| `HasAuthority()` | `GetOwner() && GetOwner()->HasAuthority()` |
| `GetName()` | `GetNameSafe(GetOwner())` 或 `GetReadableName()` |
| `DOREPLIFETIME_CONDITION(AOceanWorldManager, X, ...)` | `DOREPLIFETIME_CONDITION(UOceanWorldManagerComponent, X, ...)` |
| `bAlwaysRelevant = true`（构造函数） | **删除**——GameState 本身就是 always relevant |
| `bReplicates = true`（构造函数） | 改为 `SetIsReplicatedByDefault(true)` |

> ✅ **复制逻辑完全保留**：GameState 是 always relevant，组件的 `COND_InitialOnly` 属性会随 GameState 一起复制给所有客户端，行为和原来一致。

---

### 4.2 Invoker：SceneComponent → ActorComponent

#### 为什么要改

通过 `AddComponents` Action 注入的 `USceneComponent` **不会自动 attach 到 RootComponent**，`GetComponentLocation()` 返回世界原点，导致所有 chunk 生成在 (0,0)。

#### 改造要点

```cpp
UCLASS(ClassGroup = (Ocean), BlueprintType, Blueprintable,
       meta = (BlueprintSpawnableComponent))
class OCEANCORERUNTIME_API UOceanChunkInvokerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UOceanChunkInvokerComponent();

    /** 取代 GetComponentLocation()，直接用 Owner 位置 */
    UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
    FVector GetInvokerLocation() const;

    // ... 其余接口不变

private:
    void TickRegistration();     // 取代原 TryRegisterWithManager 的定时器回调
    // ...
};
```

```cpp
UOceanChunkInvokerComponent::UOceanChunkInvokerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bAutoActivate = true;                // ★ 关键修复：不加这行系统完全不工作
    SetIsReplicatedByDefault(true);
}

FVector UOceanChunkInvokerComponent::GetInvokerLocation() const
{
    return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}
```

#### 注册逻辑简化

原来用 `TActorIterator<AOceanWorldManager>` 全世界扫描 + 重试定时器。改成 GameState 组件后可以直接拿：

```cpp
void UOceanChunkInvokerComponent::TickRegistration()
{
    if (IsValid(RegisteredManager))
    {
        return;   // 已注册，快速返回（定时器保持运行，应对 Manager 被销毁的情况）
    }

    RegisteredManager = nullptr;   // 清掉失效引用

    AActor* OwnerActor = GetOwner();
    if (!OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }

    if (UOceanWorldManagerComponent* Manager = UOceanWorldManagerComponent::Get(this))
    {
        RegisteredManager = Manager;
        Manager->RegisterInvoker(this);
    }
}
```

> ⚠️ **定时器不再停止**。这修复了「Manager 销毁后 invoker 永久失联」的问题。1 秒一次的空转开销可忽略。

---

### 4.3 ChunkActor 调整

`AOceanChunkActor` **基本不用改**，只需三处调整：

#### (1) 把 `ChunkBaseZ` 移进复制结构体

```cpp
USTRUCT(BlueprintType)
struct OCEANCORERUNTIME_API FOceanChunkState
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
    FIntPoint ChunkCoord = FIntPoint::ZeroValue;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
    float ChunkSize = 20000.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
    int32 WorldSeed = 12345;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
    float BaseZ = 0.0f;          // ★ 新增，删掉私有的 ChunkBaseZ

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
    bool bInitialized = false;
};
```

#### (2) 加 BeginPlay 门闸

```cpp
private:
    bool bBeginPlayFinished = false;

void AOceanChunkActor::BeginPlay()
{
    Super::BeginPlay();
    bBeginPlayFinished = true;
    SetActorTickEnabled(bDrawDebugBounds);
    NotifyChunkInitialized();
}

void AOceanChunkActor::NotifyChunkInitialized()
{
    if (!bBeginPlayFinished || !ChunkState.bInitialized || bInitializationNotified)
    {
        return;
    }
    bInitializationNotified = true;
    OnChunkInitialized.Broadcast(this);
    BP_OnChunkInitialized();
}
```

**原因**：客户端执行顺序是「应用属性 → RepNotify → PostNetInit → BeginPlay」。不加门闸，任何在 `BeginPlay` 里绑定 `OnChunkInitialized` 的监听者都会错过事件。

#### (3) 网络相关性处理

**必须先确认项目用的复制系统**，三种情况：

| 复制系统 | `IsNetRelevantFor` 是否生效 | 处理方式 |
|---|---|---|
| 默认 NetDriver | ✅ 生效 | 保留现有代码，但回退路径改为 `return false` |
| **Replication Graph**（Lyra 默认） | ❌ **完全不调用** | 删除该函数，改配 `NetCullDistanceSquared` |
| Iris（UE5.7） | ❌ 不生效 | 用 Iris filter |

**Lyra 默认走 Replication Graph**，所以建议：

```cpp
AOceanChunkActor::AOceanChunkActor()
{
    // ...
    // 用剔除距离取代手写相关性
    NetCullDistanceSquared = FMath::Square(20000.0f * 8.0f);  // ChunkSize * MaxRadius
}
```

同时在 `LyraReplicationGraph` 的 `ClassRepNodePolicies` 里注册：

```ini
[/Script/LyraGame.LyraReplicationGraph]
+ClassSettings=(Class="/Script/OceanCoreRuntime.OceanChunkActor", Policy=Spatialize_Static, bAddClassRepInfoToMap=True)
```

> `Spatialize_Static` 适用于生成后不移动的 chunk；若 chunk 会跟随移动则用 `Spatialize_Dynamic`。

#### (4) SpawnParameters 的 Owner 问题

原代码 `SpawnParameters.Owner = this;`（Manager 是 `bAlwaysRelevant`），导致所有走 `Super::IsNetRelevantFor` 回退路径的 chunk 对全部客户端可见。

改成组件后 Manager 不再是 Actor，需要调整：

```cpp
FActorSpawnParameters SpawnParameters;
SpawnParameters.Owner = nullptr;     // 不设 Owner，避免相关性继承
SpawnParameters.SpawnCollisionHandlingOverride =
    ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
```

---

### 4.4 顺带修复的已知 Bug

迁移过程中一并解决的问题：

| # | 问题 | 修复方式 |
|---|---|---|
| 1 | `IsActive()` 恒为 false，chunk 完全不生成 | 构造函数加 `bAutoActivate = true` |
| 2 | Replication Graph 下 `IsNetRelevantFor` 是死代码 | 改用 `NetCullDistanceSquared` + RepGraph 配置 |
| 3 | `ChunkSize` 钳制不一致 | 统一走 `GetSafeChunkSize()` |
| 4 | Manager 销毁后 invoker 永久失联 | 重试定时器不再停止 |
| 5 | `OnRep_ChunkState` 早于 `BeginPlay` | 加 `bBeginPlayFinished` 门闸 |
| 6 | `ChunkBaseZ` 从 Actor 位置反推 | 移入 `FOceanChunkState` 复制 |
| 7 | Owner 相关性继承导致 chunk 全局可见 | `SpawnParameters.Owner = nullptr` |
| 9 | `RequestRefreshChunks` 每次全量刷新 | 改为 `bRefreshRequested` 脏标记 |
| 11 | `ChunkClass` 为空时日志刷屏 | `BeginPlay` 校验一次 + `bLoggedMissingChunkClass` |
| 12 | 多个 Manager 无防护 | GameState 组件天然单例 |
| 13 | Build.cs 缺依赖 | 补 `NetCore`、`ModularGameplay` |
| 14 | Invoker 未 attach 导致位置为原点 | 改 `UActorComponent` + `GetInvokerLocation()` |

> 编号对应此前代码审查文档。第 8、10、15 条（RPC 限流、组件查找缓存、Debug 宏包裹）属于优化项，可在迁移后单独处理。

---

## 五、接线：Experience 配置

### 5.1 创建 Experience Definition

新建蓝图，父类选 `LyraExperienceDefinition`（或继承 `B_LyraShooterGameBase`）。

命名建议：`B_OceanExperience`

### 5.2 配置 Actions

在 Experience 的 **Actions** 数组里添加一个 `GameFeatureAction_AddComponents`，配置两条 Component Entry：

| Target Actor | Component Class | Client | Server |
|---|---|---|---|
| `LyraGameState` | `OceanWorldManagerComponent` | ❌ | ✅ |
| `B_Boat_Pawn`（你的载具/角色） | `OceanChunkInvokerComponent` | ✅ | ✅ |

**说明：**

- Manager 只在服务端需要（它是权威方），勾 Server 即可。但如果客户端需要读 `WorldSeed`/`ChunkSize` 做本地生成，则 **Client 也要勾**（组件本身会复制这些属性）
- Invoker 两端都要：服务端用于计算需要哪些 chunk，客户端用于本地预测/剔除

### 5.3 关键点

`GameFeatureAction_AddComponents` 接受任意 `TSubclassOf<UActorComponent>`，**包括来自普通插件的类**。这就是为什么 OceanCore 不需要是 GameFeature。

### 5.4 其他 Experience 字段

| 字段 | 填什么 |
|---|---|
| `GameFeaturesToEnable` | 留空（我们没有 GameFeature 插件）；若将来加了内容层再填 |
| `DefaultPawnData` | 你的船/潜水员 PawnData（含输入配置、AbilitySet） |
| `ActionSets` | 复用 Lyra 现成的 HUD、通用输入 ActionSet |

---

## 六、地图接入

### 6.1 地图位置

由于 OceanCore 是普通插件，地图有两个可选位置：

| 位置 | 适用场景 |
|---|---|
| `Content/Maps/L_OceanTest.umap`（主项目） | 简单，推荐起步阶段 |
| `Plugins/OceanCore/Content/Maps/`（插件内） | 想让插件自带示例地图 |

### 6.2 Asset Manager 扫描路径

**最容易漏的一步。** Lyra 的 Asset Manager 默认只扫描特定目录。

编辑 `Config/DefaultGame.ini`：

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(PrimaryAssetType="Map",AssetBaseClass="/Script/Engine.World",bHasBlueprintClasses=False,bIsEditorOnly=True,Directories=((Path="/Game/System/DefaultLevels"),(Path="/ShooterMaps/Maps"),(Path="/Game/Maps")),SpecificAssets=,Rules=(Priority=-1,ChunkId=-1,bApplyRecursively=True,CookRule=AlwaysCook))
```

关键是往 `Directories` 里加上你的地图目录。

### 6.3 关联 Experience

打开地图 → **World Settings** → `Game Mode` 分类 → **Default Gameplay Experience** → 选 `B_OceanExperience`。

这样 PIE 直接跑这张图时会自动加载对应体验。

### 6.4 前端菜单（可选）

若要在 Lyra 主菜单选到它，需建 `LyraUserFacingExperienceDefinition` 数据资产：

| 字段 | 值 |
|---|---|
| `MapID` | 指向 `L_OceanTest`（Primary Asset Id） |
| `ExperienceID` | 指向 `B_OceanExperience` |
| `TileTitle` | "海洋冒险" |
| `TileIcon` | 缩略图 |

然后加进前端实验列表（`/Game/System/FrontEnd/` 下相关配置）。

### 6.5 打包配置

`Project Settings → Packaging → List of maps to include in a packaged build` 加上地图，或依赖 6.2 步的 `CookRule=AlwaysCook`。

---

## 七、迁移步骤清单

按顺序执行，每步完成后编译验证：

### 阶段一：建立插件骨架

- [ ] 关闭编辑器
- [ ] 新建 `Plugins/OceanCore/` 目录结构
- [ ] 写入 `OceanCore.uplugin`
- [ ] 写入 `Source/OceanCoreRuntime/OceanCoreRuntime.Build.cs`
- [ ] 迁移 `OceanCoreRuntimeModule.h/.cpp`（改日志类目名为 `LogOceanCore`）
- [ ] 重新生成项目文件（右键 `.uproject` → Generate Visual Studio / Rider project files）
- [ ] 编译，确认空模块能加载

### 阶段二：迁移核心类

- [ ] 迁移 `OceanChunkActor.h/.cpp`
  - [ ] 替换 `OCEANADVENTURERUNTIME_API` → `OCEANCORERUNTIME_API`
  - [ ] `FOceanChunkState` 加 `BaseZ` 字段
  - [ ] 加 `bBeginPlayFinished` 门闸
  - [ ] 处理 `IsNetRelevantFor`（按 4.3-(3) 的决策）
- [ ] 迁移 `OceanChunkInvokerComponent.h/.cpp`
  - [ ] 基类改 `USceneComponent` → `UActorComponent`
  - [ ] 构造函数加 `bAutoActivate = true`
  - [ ] 加 `GetInvokerLocation()`
  - [ ] 注册逻辑改为 `TickRegistration()`，定时器不停止
- [ ] 新建 `OceanWorldManagerComponent.h/.cpp`
  - [ ] 基类 `AActor` → `UActorComponent`
  - [ ] 所有 `HasAuthority()` 改为 `GetOwner()->HasAuthority()`
  - [ ] 加静态 `Get()` 辅助函数
  - [ ] 加 `GetSafeChunkSize()`
  - [ ] `RequestRefreshChunks` 改为脏标记
  - [ ] `SpawnChunk` 里 `Owner = nullptr`
- [ ] 编译通过

### 阶段三：接线

- [ ] 编辑器中启用 OceanCore 插件（Edit → Plugins）
- [ ] 创建 `BP_OceanChunk`（继承 `AOceanChunkActor`）
- [ ] 创建 `B_OceanExperience`（继承 `LyraExperienceDefinition`）
- [ ] 配置 `GameFeatureAction_AddComponents`（见第五节）
- [ ] 创建测试地图 `L_OceanTest`
- [ ] 配置 World Settings 的 Default Gameplay Experience
- [ ] 更新 `DefaultGame.ini` 的 Asset Manager 扫描路径

### 阶段四：清理

- [ ] 确认新插件工作正常后，删除旧的 `OceanAdventureRuntime` 模块
- [ ] 检查是否有遗留的 `#include "OceanAdventure..."` 引用
- [ ] 删除 `Binaries/`、`Intermediate/` 后完整重新编译一次

---

## 八、验证方法

### 8.1 基础功能验证

在 PIE 里用控制台命令逐项确认：

```
// 1. 确认 Manager 已注入 GameState
DisplayAll LyraGameState OceanWorldManagerComponent

// 2. 确认 Invoker 已注入 Pawn 且处于激活状态
DisplayAll OceanChunkInvokerComponent ActiveRadius

// 3. 打开 chunk 调试绘制
// 在 BP_OceanChunk 的 Class Defaults 里勾上 bDrawDebugBounds
```

预期结果：以 Pawn 为中心，`(2*Radius+1)²` 个 chunk 的调试框可见。默认 `ActiveRadius = 3` → **49 个 chunk**。

### 8.2 联机验证

```
// 用 Dedicated Server 模式启动 PIE
// Editor Preferences → Play → Play Net Mode → Play As Client
// Number of Players → 2
```

检查项：

- [ ] 两个客户端各自看到以自己为中心的 chunk 集合
- [ ] 服务器端 `GetActiveChunkCount()` 是两个客户端所需 chunk 的**并集**（有重叠时小于 98）
- [ ] 客户端移动时 chunk 正确加载/卸载
- [ ] 客户端断开后，其独占的 chunk 在 `UnloadGraceSeconds` 后被销毁

### 8.3 常见故障排查

| 症状 | 可能原因 |
|---|---|
| 一个 chunk 都不生成 | `bAutoActivate` 没设；或 `ChunkClass` 为空 |
| chunk 全挤在 (0,0) | Invoker 还是 `USceneComponent` 且未 attach |
| 客户端看不到 chunk | Replication Graph 未配置；或 `NetCullDistanceSquared` 太小 |
| Manager 找不到 | Experience 的 AddComponents 没勾 Server；或地图没关联 Experience |
| 编译报 `LyraGame` 相关错误 | Build.cs 误加了 `LyraGame` 依赖 |

---

## 九、未来升级路径

### 何时需要加 GameFeature 层

| 需求 | 普通插件够吗 |
|---|---|
| 只是 C++ 逻辑 + 少量资源 | ✅ 够 |
| 海图出现在前端菜单 | ⚠️ 需要 Experience，可放主项目 Content |
| 地图/资源**按需加载**，不玩就不占内存 | ❌ 需要 GameFeature |
| DLC / 赛季内容分发 | ❌ 需要 GameFeature |
| 团队多人并行开发、内容隔离 | 推荐 GameFeature |
| 复用到别的项目 / 上架 Marketplace | ✅ **必须是普通插件** |

### 升级方式（无痛）

将来若需要 GameFeature，**不用改动 OceanCore 一行代码**，只需新建一个内容层：

```
Plugins/
├── OceanCore/                      ← 保持不变
└── GameFeatures/
    └── OceanAdventure/             ← 新增
        ├── Content/
        │   ├── Maps/
        │   ├── Experiences/
        │   └── OceanAdventure.uasset    (GameFeatureData)
        └── OceanAdventure.uplugin       (依赖 OceanCore)
```

把地图和 Experience 从主项目挪进去即可。

**这正是选普通插件的价值**：升级路径通畅，而反向拆解（从 GameFeature 里把代码抠出来）要麻烦得多。

---

## 附录：迁移前后对照速查

| 项目 | 迁移前 | 迁移后 |
|---|---|---|
| 插件类型 | 无（裸模块） | 普通插件 `OceanCore` |
| Manager | `AOceanWorldManager`（Actor） | `UOceanWorldManagerComponent`（GameState 组件） |
| Invoker 基类 | `USceneComponent` | `UActorComponent` |
| Invoker 位置来源 | `GetComponentLocation()` | `GetOwner()->GetActorLocation()` |
| Manager 查找 | `TActorIterator` 全世界扫描 | `GameState->FindComponentByClass<>()` |
| 注入方式 | 手动摆放 / 无 | Experience 的 `AddComponents` Action |
| 网络相关性 | `IsNetRelevantFor` 手写 | `NetCullDistanceSquared` + RepGraph |
| Lyra 依赖 | 无 | **仍然无**（关键优势） |
