# OceanAdventureRuntime 代码审查 & UE5 无限海岛海洋生成方案

> 审查对象：`OceanAdventureRuntime` 模块（9 个文件）
> 引擎版本假设：UE 5.7 + Lyra 框架
> 生成日期：2026-08-18

---

## 目录

- [第一部分：无限海岛 + 海洋的算法方案](#第一部分无限海岛--海洋的算法方案)
  - [1. 地形高度场：噪声组合](#1-地形高度场噪声组合)
  - [2. 网格生成与 LOD](#2-网格生成与-lod)
  - [3. 海洋](#3-海洋)
  - [4. 分块与确定性](#4-分块与确定性)
- [第二部分：代码问题清单](#第二部分代码问题清单)
  - [严重问题](#-严重会导致系统完全不工作)
  - [逻辑 Bug](#-逻辑-bug)
  - [健壮性与性能](#-健壮性与性能)
- [第三部分：修复优先级建议](#第三部分修复优先级建议)

---

## 第一部分：无限海岛 + 海洋的算法方案

### 1. 地形高度场：噪声组合

**核心原则**：高度必须是世界坐标的纯函数 `H = f(worldX, worldY, seed)`，绝不能依赖 chunk 局部随机数——否则相邻 chunk 边界会裂开。

```
H(p) = IslandMask(p) * fBm(p) + OceanFloor(p)
```

| 技术 | 作用 | 说明 |
|---|---|---|
| **fBm（分形布朗运动）** | 基础起伏 | 4~8 层 Perlin/Simplex 噪声叠加，每层频率 ×2、振幅 ×0.5（persistence） |
| **Ridged Multifractal** | 山脊 | `1 - abs(noise)` 再取幂，用于岛屿中央山体 |
| **Domain Warping（域扭曲）** | 打破规整感 | `fBm(p + fBm(p) * strength)`，让海岸线出现峡湾和半岛。**性价比最高的一招** |
| **Voronoi / Worley 噪声** | 决定岛屿位置 | 把世界切成大格（如 5km），每格用 `hash(seed, cellX, cellY)` 抖动出岛心 |
| **径向衰减（Island Falloff）** | 生成"岛"而非"大陆" | `mask = 1 - smoothstep(rInner, rOuter, distToCenter)`，再乘噪声让轮廓不规则 |

**进阶选项：**

- **Poisson Disk Sampling** —— 控制岛屿分布密度，避免扎堆
- **热力/水力侵蚀（Hydraulic Erosion）** —— 迭代算法，运行时开销大。建议只对近处 chunk 做少量迭代，或用噪声近似

---

### 2. 网格生成与 LOD

| 方案 | 说明 | 适用场景 |
|---|---|---|
| **Geometry Clipmap / CDLOD** | 嵌套同心环网格，越远格子越大，顶点数恒定 | 无限地形的标准解法 |
| **UDynamicMeshComponent + GeometryScript** | UE5 原生，C++/BP 都可用 | 中等规模 |
| **RealtimeMeshComponent**（第三方） | 比 ProceduralMeshComponent 快很多，支持 LOD 和异步构建 | **推荐** |
| **GPU 高度图 + WPO** | 计算着色器生成高度图到 RenderTarget，静态细分网格在材质中用 WPO 采样偏移 | **最快**，但碰撞需 CPU 侧另算 |

**避坑提示：**

- ❌ **Landscape 不适合运行时无限生成**
- ❌ **World Partition** 是给手工搭建的世界做流送的，不是无限程序化的答案
- ❌ **Nanite** 运行时构建代价很高，一般不用于程序生成地形

---

### 3. 海洋

**方案 A：UE5 自带 Water 插件**

- Single Layer Water 着色 + **Gerstner 波**（多组波叠加）
- 自带浮力组件 `BuoyancyComponent`
- ⚠️ `WaterZone` 有边界，无限世界需要让 WaterZone 跟随玩家移动（UE 5.3+ 支持）

**方案 B：FFT 海面（Tessendorf 算法）**

- 效果参考《刺客信条：黑旗》《原子之心》
- 流程：Phillips 谱 → IFFT → 位移贴图 + 法线
- 开源实现可参考 Oceanology / WaveWorks 思路

**浮力注意事项**：必须和波形用同一套公式。Gerstner 波可解析求高度；FFT 需要回读位移贴图。

---

### 4. 分块与确定性

当前架构方向正确：**服务器只同步 seed + chunk 坐标，客户端各自确定性生成**。这是网络带宽最省的做法。

⚠️ **但要注意**：服务器也需要碰撞（船撞礁石、角色上岛），所以服务器不能完全跳过地形生成，至少要生成一份低精度高度场用于物理。

---

## 第二部分：代码问题清单

### 🔴 严重：会导致系统完全不工作

#### 1. `IsActive()` 默认为 false，所有 invoker 会被跳过

**位置**：`OceanWorldManager.cpp` → `BuildRequiredChunkSet()`

```cpp
if (!Invoker || !Invoker->IsActive())  // ← 问题所在
{
    continue;
}
```

**原因**：`UActorComponent::bAutoActivate` 默认是 `false`，`OnRegister()` 里只有 `bAutoActivate == true` 才会调 `Activate()`。构造函数没设 `bAutoActivate`，所以 `bIsActive` 永远是 false，`BuildRequiredChunkSet()` 返回空集 —— **一个 chunk 都不会生成**。

**迷惑性**：`IsNetRelevantFor()` 里并没有检查 `IsActive()`，两处逻辑不一致。

**修复**：

```cpp
UOceanChunkInvokerComponent::UOceanChunkInvokerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bAutoActivate = true;              // ← 加这行
    SetIsReplicatedByDefault(true);
}
```

> 推荐设 `bAutoActivate = true` 而非删掉 `IsActive()` 判断，这样蓝图里还能手动 Deactivate 临时停用。

---

#### 2. Lyra 用 Replication Graph，`IsNetRelevantFor` 会被完全忽略

**架构级问题。**

Lyra 自带 `ULyraReplicationGraph`，启用后 **`AActor::IsNetRelevantFor()` 根本不会被调用** —— 相关性由 Replication Graph 的节点（空间化网格节点、AlwaysRelevant 节点等）决定。

`OceanChunkActor` 里那套精心设计的 chunk 距离相关性逻辑，在 Lyra 环境下**是死代码**。

**两个方向：**

- **方案 A（简单）**：让 chunk actor 走 Replication Graph 的空间化网格
  - 把 `NetCullDistanceSquared` 设成 `(ChunkSize * MaxRadius)²`
  - 在 `LyraReplicationGraph` 的 `ClassRepNodePolicies` 里把 `AOceanChunkActor` 注册为 `Spatialize_Dynamic` 或 `Spatialize_Static`
- **方案 B（彻底）**：写自定义 Replication Graph Node，复用现有 chunk 坐标逻辑

> ⚠️ 若项目开启了 **Iris**（UE5.7 新复制系统），`IsNetRelevantFor` 同样不生效，需用 Iris 的 filter。**先确认项目实际用的是哪套复制系统。**

---

### 🟠 逻辑 Bug

#### 3. `ChunkSize` 钳制不一致

**位置**：`OceanWorldManager.cpp`

```cpp
FIntPoint AOceanWorldManager::GetChunkCoordFromWorldLocation(FVector WorldLocation) const
{
    const float SafeChunkSize = FMath::Max(100.0f, ChunkSize);   // ← 钳制了
    ...
}

FVector AOceanWorldManager::GetChunkWorldOrigin(FIntPoint ChunkCoord) const
{
    return FVector(
        static_cast<double>(ChunkCoord.X) * ChunkSize,           // ← 没钳制
        ...
}
```

**风险**：编辑器 meta `ClampMin` 只在 UI 层生效，蓝图 SetProperty 或反序列化能绕过。两个函数用的尺度不一样时，chunk 会全挤在原点附近。

**修复**：加 `GetSafeChunkSize()` 私有函数统一走它；或在 `PostInitProperties` / `BeginPlay` 里一次性钳死后不再用裸字段。

---

#### 4. Manager 销毁后 invoker 永久失联

**位置**：`OceanChunkInvokerComponent.cpp` → `TryRegisterWithManager()`

注册成功后调 `StopRegistrationRetry()`。若之后 manager 被销毁（关卡切换、Seamless Travel、手动 Destroy），`RegisteredManager` 变 invalid，但**重试定时器已停，永远不会再注册**。

**修复**：让定时器一直跑（1 秒一次，开销可忽略），把"是否已注册"作为快速返回条件：

```cpp
void UOceanChunkInvokerComponent::TickRegistration()
{
    if (IsValid(RegisteredManager))
    {
        return;   // 已注册，什么都不做
    }
    RegisteredManager = nullptr;   // 清掉失效引用
    TryRegisterWithManager();
}
```

---

#### 5. `OnRep_ChunkState` 可能早于 `BeginPlay` 触发

**位置**：`OceanChunkActor.cpp`

```cpp
void AOceanChunkActor::OnRep_ChunkState()
{
    ChunkBaseZ = GetActorLocation().Z;
    NotifyChunkInitialized();          // ← 可能在 BeginPlay 之前
}
```

**客户端实际执行顺序**：创建 Actor → 应用初始属性 → **调用 RepNotify** → `PostNetInit()` → `BeginPlay()`

所以 `OnChunkInitialized` 广播时，**任何在 `BeginPlay` 里绑定该委托的监听者都还没绑上**，会直接错过事件。

**修复**：加 BeginPlay 门闸：

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
    // ...
}
```

---

#### 6. `ChunkBaseZ` 从 Actor 位置反推，太脆弱

**位置**：`OceanChunkActor.cpp` → `OnRep_ChunkState()`

```cpp
ChunkBaseZ = GetActorLocation().Z;
```

依赖"生成位置一定在初始 bunch 里且已被应用"这个隐含假设。

**修复**：把 `ChunkBaseZ` 直接放进 `FOceanChunkState`：

```cpp
USTRUCT(BlueprintType)
struct OCEANADVENTURERUNTIME_API FOceanChunkState
{
    GENERATED_BODY()
    // ... 现有字段
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
    float BaseZ = 0.0f;      // ← 加这个，删掉私有的 ChunkBaseZ
};
```

> 整个 struct 已在复制，多一个 float 没有额外成本，还消除了对位置复制时序的依赖。

---

#### 7. `Super::IsNetRelevantFor` 回退路径会让 chunk 对所有人可见

**位置**：`OceanWorldManager.cpp` → `SpawnChunk()`

```cpp
SpawnParameters.Owner = this;   // Owner 是 bAlwaysRelevant = true 的 manager
```

`AActor::IsNetRelevantFor()` 在自身非 always relevant 时会转发给 Owner 的相关性判断。Manager 是 `bAlwaysRelevant = true`，所以**所有走到 `Super::` 回退路径的 chunk（未初始化的、找不到 invoker 的）都会对全部客户端可见**。

**修复**：回退路径显式返回 `false`；或不把 manager 设为 Owner（用 `SpawnParameters.Instigator` 或干脆不设）。

---

### 🟡 健壮性与性能

#### 8. `ServerSetActiveRadius` 缺少限流

Reliable Server RPC + 每次调用触发一次全量 `RefreshChunks()`。恶意客户端可在 Tick 里疯狂调 `SetActiveRadius(3); SetActiveRadius(4);` 打服务器。

**修复**：加时间戳限流（如每个 invoker 每 0.5 秒最多接受一次半径变更）。另外考虑加 `WithValidation`。

---

#### 9. `RequestRefreshChunks()` 应该合并而不是立即执行

现在每次 register / unregister / 半径变更都同步跑一次完整 `RefreshChunks()`。50 个玩家同时进图 = 50 次全量刷新，每次遍历所有 invoker × radius²。

**修复**：改成置脏标记，由已有的 `RefreshTimer` 统一处理：

```cpp
void AOceanWorldManager::RequestRefreshChunks()
{
    if (HasAuthority())
    {
        bRefreshRequested = true;   // 下一个 timer tick 会处理
    }
}
```

---

#### 10. `IsNetRelevantFor` 里每次都做 `FindComponentByClass`

`FindComponentByClass` 是对组件数组的线性扫描。相关性检查是 **每个 actor × 每个连接 × 每次网络更新**，chunk 数量一多开销可观。

**修复**：缓存 `PlayerController → InvokerComponent` 映射，在 pawn 换人时失效。

> 若按第 2 点改用 Replication Graph，这段代码本来就要重写。

---

#### 11. `ChunkClass` 为空时错误日志会刷屏

`SpawnChunk` 里的 `UE_LOG(..., Error, ...)` 会在每次刷新（0.5 秒）× 每个需要的 chunk（radius 3 = 49 个）触发 —— **每秒近 100 条 Error 日志**。

**修复**：`BeginPlay` 里检查一次并禁用管理器，或用 `bLoggedMissingClass` 标志。

---

#### 12. 多个 Manager 没有防护

`TActorIterator<AOceanWorldManager>` 取第一个找到的。关卡里若放了两个 manager，两个都会生成 chunk，坐标冲突。

**修复**：`BeginPlay` 里检测并警告；或改用 `UWorldSubsystem` 天然保证单例。

---

#### 13. `Build.cs` 建议补充依赖

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core", "CoreUObject", "Engine",
    "NetCore",          // 复制相关，UE5 的标准做法
});
```

后续若要接 Lyra 的 GAS / GameFeature，还需要 `GameplayTags`、`ModularGameplay`、`GameFeatures`。

---

#### 14. Invoker 是 `USceneComponent` 但没有兜底

若有人用 C++ `CreateDefaultSubobject` 创建但忘了 `SetupAttachment(RootComponent)`，`GetComponentLocation()` 会返回世界原点，chunk 全部生成在 (0,0)。

**修复**：`BeginPlay` 里检查 `GetAttachParent()`，没有就 fallback 到 `GetOwner()->GetActorLocation()` 并打 Warning。

---

#### 15. Debug 绘制建议用宏包裹

```cpp
#if ENABLE_DRAW_DEBUG
    // Tick 里的 DrawDebugBox / DrawDebugString
#endif
```

Shipping 构建里 DrawDebug 函数是空实现，但 Tick 本身和那些向量计算还是会跑。

---

## 第三部分：修复优先级建议

| 优先级 | 问题编号 | 说明 |
|---|---|---|
| **P0（立即）** | #1 | 不修复系统完全不工作 |
| **P0（立即）** | #2 | 决定相关性代码要不要重写，影响后续所有网络设计 |
| **P1（尽快）** | #4, #5, #7 | 会导致间歇性、难以复现的 bug |
| **P2（迭代中）** | #3, #6, #12, #14 | 健壮性问题，当前配置下不一定暴露 |
| **P3（优化期）** | #8, #9, #10, #11, #13, #15 | 性能与工程质量 |

---

## 总体评价

**架构方向正确**：服务器权威管理 chunk 生命周期 + 只复制 seed 让客户端确定性生成，这是无限世界联机的正确思路。

**代码质量良好**：弱指针、`TGuardValue` 防重入、authority 检查、宽限期卸载都做得不错。

**主要问题集中在两处**：

1. 第 1 条（`IsActive()`）让系统现在跑不起来
2. 第 2 条（Replication Graph）决定了相关性那部分代码的去留

**建议**：先确认项目实际使用的复制系统（默认 NetDriver / Replication Graph / Iris），再决定 `IsNetRelevantFor` 那段的处理方式。
