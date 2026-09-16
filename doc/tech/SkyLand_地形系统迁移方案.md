# SkyLand 台阶地形系统迁移方案

> 目标：把 SkyLand（Vite + TypeScript + Three.js + Rust/WASM 的 Web 原型）里的**离散台阶格地形**，
> 迁移进 OceanAdventure（UE 5.7 + Lyra）的 `OceanCore` 通用插件。
>
> 生成日期：2026-09-15
> 适用引擎：UE 5.7 + Lyra
> 源工程：`SkyLand`，主要在 `shared/world/`、`server/scene/`、`src/world/`
> 目标模块：`Plugins/OceanCore/Source/OceanCoreRuntime/`
> 相关文档：`doc/tech/OceanAdventure_代码审查与无限地形方案.md`、`doc/tech/OceanCore_插件迁移方案.md`、
> **`doc/tech/Line-Art-Style-UE5-Mobile-Rendering.md`（线稿风格，本方案的渲染前提）**

---

## 目录

- [一、结论与范围](#一结论与范围)
- [二、两边现状对照](#二两边现状对照)
- [三、目标架构](#三目标架构)
- [四、逐层迁移细则](#四逐层迁移细则)
  - [4.1 真相层：格 code 的位打包](#41-真相层格-code-的位打包)
  - [4.2 整数哈希与值噪声](#42-整数哈希与值噪声)
  - [4.3 形状推导](#43-形状推导)
  - [4.4 采样 API](#44-采样-api)
  - [4.5 稀疏编辑覆盖层](#45-稀疏编辑覆盖层)
  - [4.6 折边：线稿风格的接口](#46-折边线稿风格的接口)
  - [4.7 网格与碰撞](#47-网格与碰撞)
  - [4.8 接回 OceanCore 的 chunk 流送](#48-接回-oceancore-的-chunk-流送)
  - [4.9 与现有连续高度场的关系](#49-与现有连续高度场的关系)
- [五、开工前必须定的三个决策](#五开工前必须定的三个决策)
- [六、坑清单](#六坑清单)
- [七、分阶段实施步骤](#七分阶段实施步骤)
- [八、验证方法](#八验证方法)
- [九、不在本方案范围内](#九不在本方案范围内)

---

## 一、结论与范围

**这两套地形不是同一类东西，没有可以直接复制的代码，能迁移的只有数据模型与算法。**

SkyLand 的地形是 2 米格 × 1 米层的**离散台阶地形**（平台 + 直坡 + 角坡），用 JS/TS 写一份、
Rust/WASM 再镜像一份；OceanAdventure 现有地形是 `H = IslandMask × fBm + OceanFloor` 的
**连续光滑高度场**，落在 `UOceanGenerationSettings` 里。中间隔着语言、引擎、坐标系、单位四层，
逐行翻译是唯一路径，工作量集中在把算法翻对、把边界条件翻对。

反过来说，**`OceanCore` 现在缺的那一块，正好是 SkyLand 最成熟的那一块**：
`AOceanChunkActor` 目前只有一个 `SceneRoot` + 复制状态 + `BP_OnChunkInitialized`，
**没有任何网格生成，也没有任何碰撞体**，地形的可见化完全压在蓝图上。
SkyLand 那边则有一整套「渲染与碰撞共用同一份三角形拓扑」的实现，外加可编辑地形与编辑同步。

### 该搬什么（按价值排序）

| # | 内容 | 源文件 | 为什么值得搬 |
|---|---|---|---|
| 1 | 格 code 的位打包约定 | `shared/world/terrainConfig.mjs` | 整个系统的地基，纯约定，照抄即可 |
| 2 | 整数哈希 + 值噪声 | `shared/world/hash.mjs` | **全仓库最容易迁移的部分**，零浮点、位级等价，逐位一致 |
| 3 | 形状推导规则 | `shared/world/terrainContent.mjs` | 靠手感调出来的规则，重写不如翻译 |
| 4 | 网格拓扑与崖面归属 | `shared/world/terrainCollisionMesh.mjs` | 边界条件踩错就是 chunk 之间裂缝 |
| 5 | 稀疏编辑覆盖层 | `shared/world/terrainPatches.mjs`、`terrainEditing.mjs` | 决定可编辑地形的内存占用与同步带宽 |

### 不要搬

| 内容 | 理由 |
|---|---|
| WASM / JS 双后端与 parity 测试机制 | 双实现的存在理由是「浏览器可能加载不了 wasm」。UE 侧只有一份 C++，理由消失 |
| Rapier 碰撞代码（`shared/physics/`） | 换成 UE 的 `UBodySetup` + trimesh |
| Three.js 渲染层（`src/world/TerrainChunkView.ts` 的 `renderOrder` 排序技巧） | UE 的半透明排序是另一套机制 |
| chunk 流送调度（`shared/world/chunkStream.mjs`） | `UOceanWorldManagerComponent` + `UOceanChunkInvokerComponent` 已经在做这件事 |
| 物件合批（`shared/world/chunkGenerator.mjs` 把树石合成一个 draw call） | UE 用 HISM，白送 |

---

## 二、两边现状对照

| | SkyLand `shared/world/` | OceanAdventure `Plugins/OceanCore/` |
|---|---|---|
| 地形本质 | 2m 格 × 1m 层的台阶地形，13 种形状（平 / 四向直坡 / 八种角坡） | 连续高度场 `H = IslandMask × fBm + OceanFloor` |
| 真相函数 | `terrainCellCodeAt(worldSeed, cellX, cellZ) -> int32` | `UOceanGenerationSettings::SampleTerrainHeight(FVector2D) -> float` |
| 一格的内容 | 高度层 `int8` + 表面（地/水）1 bit + 群系 3 bit + 形状 4 bit，压在一个 int 里 | 只有高度 |
| 随机 | **纯整数** `hash32` / `valueNoise`，全程零浮点 | 浮点噪声 |
| 编辑 | 稀疏覆盖层 `TerrainPatchStore`，只存改过的格，单格编辑 O(1) | **无**，地形不可编辑 |
| chunk | 32m = 16×16 格 | 200m（`ChunkSize = 20000cm`） |
| 网格 | `buildTerrainCollisionMeshFromCodes` 统一产出，渲染与碰撞共用 | **无** |
| 碰撞 | Rapier trimesh（明确放弃 heightfield，因为存在 1m 垂直崖面） | **无** |
| 流送 | `planChunkStream`（加载半径 / 保留半径带滞回） | `UOceanWorldManagerComponent`（`RefreshInterval` + `UnloadGraceSeconds`） |
| 同步 | 服务端权威，`room:terrain` 只下发 patch 格；地形本体各端自算 | 复制 `FOceanChunkState`（seed + 坐标 + 设置资产） |

---

## 三、目标架构

按 `AGENTS.md` 的模块分层，本方案的全部产出属于**通用框架层**，落在 `OceanCore` 普通插件里，
**不得依赖 `LyraGame`、`GameplayAbilities`、`CommonUI` 或任何 GameFeature**。
判据仍是那一条：作弊命令、存档恢复、编辑器工具都不经过 GAS，它们必须能直接调用框架 API。

```
Plugins/OceanCore/Source/OceanCoreRuntime/
├── Public/Terrain/
│   ├── OceanTerrainTypes.h           ← terrainConfig.mjs（枚举 + 位打包 + 常量）
│   ├── OceanTerrainContent.h         ← terrainContent.mjs（真相函数）
│   ├── OceanTerrainWater.h           ← terrainWater.mjs + terrainSupport.mjs
│   ├── OceanTerrainPatchStore.h      ← terrainPatches.mjs
│   ├── OceanTerrainEditor.h          ← terrainEditing.mjs
│   ├── OceanTerrainOutline.h         ← 新增：折边查询，线稿方案 §2.4
│   ├── OceanTerrainMeshBuilder.h     ← terrainCollisionMesh.mjs
│   └── OceanTerrainChunkComponent.h  ← 新增：网格 + 碰撞的宿主组件
└── Private/Terrain/
    ├── OceanTerrainHash.cpp          ← hash.mjs（内部实现，不进 Public）
    ├── OceanTerrainContent.cpp
    ├── ...
    └── Tests/
        ├── OceanTerrainParityTest.cpp
        └── Fixtures/terrain-parity.txt    ← 从 SkyLand 导出的逐格基准
```

分层依赖（单向）：

```
OceanTerrainChunkComponent（引擎耦合：网格/碰撞/Actor）
        │
        ▼
OceanTerrainMeshBuilder（只吃格子码，吐顶点/索引）
        │
        ▼
OceanTerrainPatchStore / OceanTerrainEditor（稀疏覆盖）
        │
        ▼
OceanTerrainContent（纯函数真相层）
        │
        ▼
OceanTerrainHash（整数哈希，无任何引擎依赖）
```

**底下两层不碰 `UObject`、不碰 `UWorld`、不碰任何引擎类型**（`int32` / `FIntPoint` 这类 POD 除外），
这是它们能被自动化测试逐格比对的前提，也是编辑器工具能绕过运行时直接调用的前提。

---

## 四、逐层迁移细则

### 4.1 真相层：格 code 的位打包

SkyLand 把一格的四项属性压进一个 int32：

| 位段 | 内容 | 取值 |
|---|---|---|
| `[15:8]` | 高度层，有符号 int8 | -128..127（当前生成器只用 -2..2，其余留给编辑） |
| `[7:5]` | 群系 | 草原 / 沙 / 泥 / 雪 / 岩，共 5 种，位宽留了 8 种 |
| `[4]` | 表面 | 0 = 地面，1 = 水 |
| `[3:0]` | 形状 | 13 种 |

低 8 位（群系 + 表面 + 形状）能整块塞进 `uint8`，高 8 位是高度——这个切分是刻意的，
`buildTerrainChunkData` 就是靠它把一个 chunk 压成 `Int16Array heights + Uint8Array meta`。
**迁移时保持同样的切分**，之后做网络压缩、存档、编辑器预览都要靠它。

```cpp
// OceanTerrainTypes.h
UENUM(BlueprintType)
enum class EOceanTerrainSurface : uint8 { Ground = 0, Water = 1 };

UENUM(BlueprintType)
enum class EOceanTerrainBiome : uint8 { Grassland = 0, Sand = 1, Mud = 2, Snow = 3, Rock = 4 };

UENUM(BlueprintType)
enum class EOceanTerrainShape : uint8
{
    Flat = 0,
    RampNorth = 1, RampEast = 2, RampSouth = 3, RampWest = 4,
    CornerHighNorthEast = 5, CornerHighSouthEast = 6,
    CornerHighSouthWest = 7, CornerHighNorthWest = 8,
    CornerLowNorthEast  = 9, CornerLowSouthEast  = 10,
    CornerLowSouthWest  = 11, CornerLowNorthWest = 12,
};

namespace OceanTerrain
{
    // 单位换算：SkyLand 用米，UE 用厘米。
    constexpr float CellSize    = 200.0f;   // 2m
    constexpr float HeightStep  = 100.0f;   // 1m，2m 跨度对应约 26.6 度斜坡
    constexpr int32 ChunkGrid   = 16;       // 每 chunk 16×16 格，见第五节决策二

    constexpr int32 ShapeMask     = 0b1111;
    constexpr int32 SurfaceShift  = 4;
    constexpr int32 BiomeShift    = 5;
    constexpr int32 BiomeMask     = 0b111;
    constexpr int32 HeightShift   = 8;

    int32 EncodeCell(int32 HeightLevel, EOceanTerrainSurface, EOceanTerrainShape, EOceanTerrainBiome);
    int32 CellHeightLevel(int32 Code);      // 注意符号扩展
    EOceanTerrainSurface CellSurface(int32 Code);
    EOceanTerrainBiome   CellBiome(int32 Code);
    EOceanTerrainShape   CellShape(int32 Code);
}
```

⚠️ `CellHeightLevel` 的符号扩展在 JS 里写成 `((code >>> 8) << 24) >> 24`。
C++ 里直接 `static_cast<int8>((Code >> HeightShift) & 0xFF)` 即可，
但**不要**写成 `(Code >> 8)` 就完事——高位还挂着别的东西时会串。

`EncodeCell` 的群系参数在 SkyLand 里有个默认值（草原），是为了兼容手写 code 的旧测试与编辑器。
**C++ 侧不要保留这个默认值**：让所有调用点显式传，否则「抬高一格雪地」会把它变回草地，
而这种错误在数值上完全合法，测试抓不到。

### 4.2 整数哈希与值噪声

`hash.mjs` 顶上那段注释是这次迁移里最该被原样搬过去的东西：

> 这里刻意不使用任何浮点运算：所有中间结果都是 32 位整数，
> JS 的 `Math.imul` / `>>>` 与 Rust 的 `wrapping_mul` / `>>` 在位级完全等价，
> 所以同一个种子在浏览器、房间进程和 WASM 里得到的世界必然一致。
> 一旦这里引入浮点，跨端就可能出现「你看到树、我看不到树」的分裂。

C++ 的 `uint32_t` 乘法本来就是 wrapping 的，`Math.imul` 逐位对应，翻译几乎是机械的：

```cpp
// OceanTerrainHash.cpp
uint32 Hash32(uint32 Seed, int32 A, int32 B, int32 C)
{
    uint32 H = Seed ^ 0x9e3779b9u;
    H = (H ^ static_cast<uint32>(A)) * 0x85ebca6bu;
    H ^= H >> 13;
    H = (H ^ static_cast<uint32>(B)) * 0xc2b2ae35u;
    H ^= H >> 16;
    H = (H ^ static_cast<uint32>(C)) * 0x27d4eb2fu;
    H ^= H >> 15;
    return H;
}
```

`valueNoise` 是整数双线性插值 + 定点 smoothstep，结果落在 `[0, 255]`。
翻译时注意两点：

- JS 的 `| 0` 是**向零取整**，C++ 的整数除法对正数一致，但**对负数不一致**。
  `smoothWeight` 与最后三次插值的输入在 `valueNoise` 里都已保证非负（`x - (latticeX << shift)`
  在算术右移后必然落在 `[0, size)`），所以这里安全——但**必须在翻译时确认过再往下走**，
  不要默认安全。
- `latticeX = x >> shift` 用的是**算术右移**，负坐标向下取整。C++ 对有符号数右移在 C++20 起
  明确定义为算术移位，UE 的目标平台也都如此，直接用 `>>` 即可。
- 注释里写明 `shift` 不要超过 6，否则 `corner * size` 溢出 32 位。C++ 侧加一条
  `static_assert` 或 `check()` 把它钉住。

**绝对不要**换成 `FMath::PerlinNoise2D` 或 `FRandomStream`。前者是浮点，后者的序列语义跟这里
不是一回事，换掉就等于放弃确定性。

### 4.3 形状推导

`terrainCellCodeAt` 的规则，逐条列出来方便翻译时对照：

1. 出生点附近 `|cellX| <= 5 && |cellZ| <= 5` 强制平坦（22×22 米的安全地）。
2. 否则取 `valueNoise(seed ^ 0x74c319ad, cellX, cellZ, shift=5)`（特征跨度 32 格 ≈ 64 米），
   按 `28 / 72 / 166 / 220` 四个阈值切成 `-2 / -1 / 0 / 1 / 2` 五层。
   **阈值和盐值都必须逐位照抄**，改一个数就是另一个世界。
3. 高度层 < 0 直接返回水面平格。
4. 否则按 `hash32(seed, cellX, cellZ, 0x2b916e47) & 3` 取一个起始方向做**稳定轮换**，
   避免所有转角都偏向同一方向：
   - 两个**相邻正交**邻居恰好高一层 → 单低角（`CornerLow*`）；
   - 单个正交邻居高一层 → 直坡（`Ramp*`）；
   - 只有对角邻居高一层 → 单高角（`CornerHigh*`）；
   - 都不满足 → 平格。
5. 群系由 `terrainBiomeAt` 单独算，**与高度完全无关**——水底也带着它所在片区的地皮，
   抽干之后露出来的是同一片地。这条语义要保住。

注意 4 里比较的是 `terrainBaseLevelAt(邻居) === heightLevel + 1`，是**恰好高一层**，
不是「更高」。写成 `>=` 会让两层以上的落差也长出坡来，崖面就没了。

### 4.4 采样 API

三个查询入口，UE 侧建议都做成 `BlueprintPure` 的静态函数或 subsystem 方法：

| SkyLand | 用途 | UE 侧建议签名 |
|---|---|---|
| `sampleTerrain(seed, x, z, target, cellCodeAt)` | O(1) 表面采样，返回高度 + 法线 + 表面 + 群系 + 可行走 | `FOceanTerrainSample SampleTerrain(FVector2D WorldXY) const` |
| `terrainCellCornerHeight(code, cx, cz)` | 指定角点高度，网格与碰撞都用它 | `float CellCornerHeight(int32 Code, int32 CornerX, int32 CornerY)` |
| `terrainCellTopHeight(code)` | 一格四角里最高的那个 | `float CellTopHeight(int32 Code)` |

`terrainCellTopHeight` 的存在理由值得在 C++ 注释里保留：地基要盖住整格就得放在最高角上，
放在格心高度的话，斜坡格上地基会有一半陷进坡里。**建造系统对接地形时用的就是这个函数**，
`BuildingCore` 那边接进来时别自己另算一个。

水体判定（`terrainWater.mjs`）的语义也要原样保留，它和连续高度场的直觉正好相反：

> 格子高度永远表示地面/海床；`WATER` 只表示该格属于**已连通水域**。
> 海平面不会修改海床，也不会让所有低于它的普通地面自动积水。

也就是说 `TerrainWaterDepth` 先看 `Surface == Water`，再算 `SeaLevel - GroundZ`；
一块低于海平面的 `Ground` 格是**干的洼地**，不是水塘。这条在 UE 侧很容易被「顺手优化」掉，
改掉之后表现是「挖个坑就自动灌水」，而且没有任何断言会响。

`sampleTerrain` 里那套角坡法线的推导（`followsX`、`derivativeSign`、`lowCorner`）是全文件最容易
翻错的一段。建议**先翻译、再用 4.7 的网格法线做交叉验证**：同一格上，解析法线与三角形面法线
在同一半边内应当一致。

### 4.5 稀疏编辑覆盖层

`TerrainPatchStore` 的模型：默认世界仍由 `(worldSeed, cellX, cellZ)` 纯函数生成，
**只保存与默认值不同的格子，并按 chunk 分桶**。内存与「被玩家真正编辑过的格数」成正比，
与世界面积无关。

```cpp
// OceanTerrainPatchStore.h
class OCEANCORERUNTIME_API FOceanTerrainPatchStore
{
public:
    explicit FOceanTerrainPatchStore(int32 InWorldSeed);

    int32 CellCodeAt(int32 GlobalCellX, int32 GlobalCellY) const;   // 有覆盖取覆盖，否则走程序化
    bool  HasCell(int32 GlobalCellX, int32 GlobalCellY) const;
    bool  SetCellCode(int32 GlobalCellX, int32 GlobalCellY, int32 Code);
    bool  ResetCell(int32 GlobalCellX, int32 GlobalCellY);
    void  ReadChunk(FIntPoint ChunkCoord, TArray<int32>& OutFlatTriples) const;

private:
    int32 WorldSeed;
    TMap<FIntPoint, TMap<int32, int32>> ChunkBuckets;   // chunk -> (localIndex -> code)
};
```

两个必须保留的行为：

- **改回默认值要把条目删掉**。`SetCellCode` 写入与程序化结果相同的 code 时，
  store 的 size 应当回到 0，而不是留一条恒等覆盖。SkyLand 的 `terrainEditing.test.mjs` 里
  专门有一条断言盯着这件事（`回到默认就不该继续占状态`），UE 侧照做。
- **变更通知按 chunk 局部派发**。一格的编辑会影响到相邻 chunk 的网格（因为崖面和 17×17 的
  采样窗口跨界），`TerrainPatchChange.affectedChunks` 就是干这个的。漏派发的现象是
  「改了地形，隔壁 chunk 的接缝没跟着更新」。

`TerrainEditor` 是门面层（`raise` / `lower` / `flood` / `setRamp` / `flatten` / `reset`），
它做的是参数校验 + 语义组合，最后都落到 `SetCellCode`。这一层可以先只搬 `raise` / `lower` /
`setRamp` / `reset`，够建造系统用；`flood` 涉及连通水域判定，可以放到后面。

### 4.6 折边：线稿风格的接口

这一节是 `Line-Art-Style-UE5-Mobile-Rendering.md` §2.4 在地形上的落点，**本方案的渲染前提由那份文档定**：
平涂填充 + 同一份几何的反转外壳两次绘制，而不是给网格挂一个材质。

反转外壳只画得出**剪影**。平格接斜坡的那道折边、角坡自己那条对角折线，它一条都画不出来——
而这些恰恰是台阶地形之所以看起来是台阶地形的东西。少了它们，整片地面会塌成一块带轮廓的色斑。

通常的补救是 Depth + Normal 的 Sobel 后处理，在移动端 Forward 下要自己写一张 normal RT、
再多一遍全屏带宽。**地形不需要**：形状枚举封闭在 13 项，任意两格之间有没有折边、是折边还是崖面，
从两个格子码就能算出来，完全不跑拓扑边提取。这是整套风格里最便宜的一条线，
也是「形状枚举值得逐位照搬」这件事在渲染侧的回报。

```cpp
// OceanTerrainOutline.h
enum class EEdgeInk : uint8
{
    None  = 0,   // 两格共面：不画线，也没有崖面几何
    Fold  = 1,   // 同高不同坡 —— 反转外壳画不出来的那条
    Cliff = 2,   // 角点高度不同：一个垂直面，图上最重的一条线
};

EEdgeInk EdgeInkBetween(int32 CodeA, int32 CodeB, ECellDirection DirectionFromA);
bool     CellDiagonalIsCrease(int32 Code);   // 八种角坡为真，平格与四种直坡为假
void     CellTopTriangles(int32 Code, FCellTriangle& OutFirst, FCellTriangle& OutSecond);
FVector  CellTriangleNormal(int32 Code, int32 TriangleIndex);
```

三条实测确认过的语义，写下来免得后来者按直觉改：

- **平格接平格 = `None`。** 这条必须守住：一旦它出线，整片平地会变成方格纸。
- **斜坡的坡脚接平地 = `Fold`，不是 `None`。** 两边同高，但一边水平一边倾斜——
  这正是让斜坡看起来是斜坡、而不是一块颜色略深的草地的那条线。
- **同向斜坡并排、以及顺坡叠成阶梯 = `None`。** 一道长坡只画它最外侧的两条折边，
  中间不画。否则每个山坡都会被画成格子。

`CellTopTriangles` 同时是 4.7 网格构建的输入——**填充网格、碰撞体、折边线条三者共用同一套拓扑**，
这是 SkyLand 那边刻意维持的不变量，UE 侧不要分叉。

⚠️ **绕序**：`CellTopTriangles` 返回的是参考实现的角点顺序，在 SkyLand 的右手 Y-up 系里是正面。
第五节决策三的轴映射交换了两个轴、因而翻转了手性，所以直接把这个顺序写进索引缓冲会得到朝内的三角形。
**在写入缓冲的那一处翻转绕序，并且只在那一处翻。** `CellTriangleNormal` 不受这个选择影响——
它总是返回朝上的那个方向。

### 4.7 网格与碰撞

这是 `OceanCore` 现在完全空白、收益最直接的一块。

**采样窗口是 17×17，不是 16×16。** 网格按格铺三角形，但**东、北两侧的崖面归本格所有**，
所以要多读一行一列，窗口是闭区间 `[0, ChunkGrid]`。这个 `TERRAIN_CHUNK_CODE_SPAN = TERRAIN_GRID + 1`
的约定踩错，现象就是 chunk 之间出现一圈裂缝。

每格产出：

- **顶面 2 个三角形**，对角线方向由形状决定——`CornerHighNorthWest` / `CornerHighSouthEast` /
  `CornerLowNorthWest` / `CornerLowSouthEast` 走 NW-SE 对角线，其余走 SW-NE
  （`usesNorthWestSouthEastDiagonal`）。切错方向的现象是角坡上出现一道折痕。
- **东侧崖面**：本格的 SE/NE 角与东邻格的 SW/NW 角高度不等时补 2 个三角形。
- **北侧崖面**：同理，本格 NW/NE 与北邻格 SW/SE。

```cpp
// OceanTerrainMeshBuilder.h
struct FOceanTerrainMeshData
{
    TArray<FVector3f> Vertices;
    TArray<uint32>    Indices;
    int32 TriangleCount = 0;
};

class OCEANCORERUNTIME_API FOceanTerrainMeshBuilder
{
public:
    /** Codes 必须是 (ChunkGrid+1)² 的窗口，原点为 chunk 的西南角。 */
    static void BuildChunkMesh(FIntPoint ChunkCoord, TArrayView<const int32> Codes,
                               FOceanTerrainMeshData& Out);
};
```

**只有这一份实现。** SkyLand 那边把它写成「`buildTerrainCollisionMesh` 是
`buildTerrainCollisionMeshFromCodes` 的包装」，注释里写明了理由：两份拓扑各自演化的话，
客户端走 worker、服务端走回调，两边的地面就会悄悄长得不一样。
UE 侧同理：**渲染网格与碰撞体必须消费同一份 `FOceanTerrainMeshData`**，不要为了性能在渲染侧
另写一份「简化版」。

碰撞用 trimesh，**不要用 heightfield**。源文件里那句注释是结论：
`Heightfields cannot represent SkyLand's vertical one-metre cliff faces; use a trimesh.`
台阶地形每一处崖面都是垂直的，高度场表达不了。

宿主组件建议：

| 用途 | 组件 | 说明 |
|---|---|---|
| 渲染 | `UDynamicMeshComponent` | UE 原生，C++/BP 都可用，改起来快 |
| 渲染（备选） | `RealtimeMeshComponent` | 第三方，支持 LOD 与异步构建，`OceanAdventure_代码审查与无限地形方案.md` 推荐的就是它 |
| 碰撞 | 同一组件的 `UBodySetup` trimesh | 顶点与渲染共用 |

顶点去重：SkyLand 用 `"x,y,z"` 字符串做 key 的 Map。C++ 侧换成
`TMap<FVector3f, uint32>`（`FVector3f` 有 `GetTypeHash`）或者定点量化后的整数 key——
格点高度都是 `HeightStep` 的整数倍，**量化成整数 key 比浮点哈希更稳**，也顺手消掉了
「同一个角点因浮点误差生成两个顶点」的隐患。

### 4.8 接回 OceanCore 的 chunk 流送

`UOceanWorldManagerComponent` 那套已经在跑，本方案**不改它的调度逻辑**，只挂载：

1. `AOceanChunkActor` 加一个 `UOceanTerrainChunkComponent`。
2. 服务端在 `InitializeChunk` 之后建网格 + 碰撞；客户端在 `OnRep_ChunkState` 之后建同一份。
   **地形本体不过网络**——两端拿着同一个 `WorldSeed` 各自算，这是 SkyLand 的做法，也是
   `OceanAdventure_代码审查与无限地形方案.md` 第 4 节认可的方向。
3. 过网络的只有 patch：对应 SkyLand 的 `room:terrain` 消息，
   载荷是 `[globalCellX, globalCellY, code, ...]` 的扁平三元组。
   新玩家加入时全量下发该区域的覆盖（对应 `readTerrainPatches`），之后增量广播。

⚠️ 建网格要放到异步任务里。一个 16×16 的 chunk 是 289 次查表 + 约 512~1500 个三角形，
单块不重，但流送时是**一批一起来**。SkyLand 那边专门为此把网格构建扔进了 worker
（`src/world/terrainCollision.worker.ts`），并且为了让它能过线程边界，把「回调式采样」
改成了「种子 + 覆盖格数组」两个可序列化的输入（`buildTerrainChunkCodes`）。
**UE 侧同样要保持 builder 的输入是纯数据**，这样它能直接进 `AsyncTask` / `UE::Tasks`，
不需要在工作线程上碰 `UObject`。

### 4.9 与现有连续高度场的关系

`UOceanGenerationSettings` 现在同时负责三件事：地形高度、岛屿遮罩、水面波形。
迁移之后**地形高度那一支被台阶地形取代，水面波形那一支原样保留**（Gerstner 波 + 浮力
和台阶地形没有冲突，且 `NavalCore` 的浮力已经在用）。

建议的过渡形态：

- `SampleTerrainHeight` / `SampleIslandMask` **保留但标记为 deprecated 的连续路径**，
  在台阶地形接管前继续给现有蓝图供数据；
- 新增 `UOceanTerrainSettings` 承接台阶地形的参数（noise 盐值、阈值、cell/step 尺寸、群系配置）；
- 岛屿遮罩这一层**值得保留并叠加**——SkyLand 的地形是「无边大平原 + 起伏」，没有岛的概念，
  而 OceanAdventure 是海岛玩法。落地方式是让 `terrainBaseLevelAt` 的噪声在岛屿遮罩之外
  直接返回负层（水），遮罩内部才切台地。**这是本方案里唯一需要新写而非翻译的算法**。

---

## 五、开工前必须定的三个决策

### 决策一：是否保留连续高度场 —— 已拍板

`Line-Art-Style-UE5-Mobile-Rendering.md` §0「本轮同时定下、但尚未动工的两条」已经定了：
**地形换成 SkyLand 的 2 米方块（13 形状枚举 + 地形编辑），`OceanCore` 的噪声高度场退役或只留远景。**

剩下的只是接缝怎么处理。台阶地形与光滑海底混在一个世界里可行，但**接缝处的法线与碰撞要单独处理**：

| 选项 | 说明 | 代价 |
|---|---|---|
| A. 全台阶 | 海床也是台阶格，只是高度层为负 | 最简单，与 SkyLand 一致；深海区域浪费格子 |
| B. 岛内台阶 + 岛外平底 | 遮罩外直接给固定深度 | 接缝在水下，看不见，**推荐** |
| C. 岛内台阶 + 岛外连续 fBm | 保留现有海底起伏 | 两套地形要在接缝处对齐高度，最麻烦 |

**推荐 B**：接缝全在水面以下，视觉上不存在，碰撞上只需要一圈裙边。
同一节还定了第二条——**服务端权威改用 Lyra/UE 原生复制**，不再保留「两端同一份 Rapier WASM」，
这条直接影响 P4，届时按它走。

### 决策二：chunk 尺寸

SkyLand 是 32m / 16×16 格；`OceanCore` 现在是 200m。两边不能都要：

| 方案 | chunk 边长 | 每 chunk 格数 | 问题 |
|---|---|---|---|
| 保持 16×16 | 32m | 256 | actor 数量相对现状翻约 39 倍 |
| 保持 200m | 200m | 10000 | 单块网格构建开销与卸载粒度都要重估 |
| **折中：32×32** | **64m** | **1024** | actor 数量约为现状的 10 倍，单块仍可异步构建 |

**建议 32×32 格 / 64m**，但这个数**必须结合实际视距预算实测后再定**，
并且一旦定下就要同时更新 `ChunkGrid` 常量与 `UOceanWorldManagerComponent` 的 `ChunkSize`——
这两个数一旦不一致，chunk 坐标与格坐标就对不上，现象是地形整体错位。

### 决策三：坐标轴映射

SkyLand 是 Three.js 右手 Y-up，约定 `+Z = NORTH`、`+X = EAST`；UE 是左手 Z-up。
**P1 采用的映射**：

```
SkyLand (x, y, z)  →  UE (X, Y, Z) = (x, z, y)
    x = EAST       →  X
    z = NORTH      →  Y
    y = UP         →  Z
```

初版方案里写的是 `(z, x, y)`（把 NORTH 对到 UE 的 `+X`，贴近「+X 是前方」的引擎惯例）。
落地时换成了上面这个：两者都是镜像、都同样要翻绕序，但 `(x, z, y)` **不需要交换格坐标的下标**，
`CellX` 就是 SkyLand 的 `cellX`，parity 基准可以直接逐格读。少一处下标交换，就少一类
「地形整体转了 90 度、但每一格看着都对」的 bug。代价只是枚举名里的 NORTH 指向 UE 的 `+Y` 而非 `+X`——
这些是世界的罗盘名，本来就是任意的，和参考实现对齐比迎合一个没别的东西依赖的惯例更值钱。

⚠️ 这是一次**镜像**（右手系到左手系），**三角形绕序必须翻转**，否则整块地形法线朝下、
背面剔除把地面剔没。翻转只在「索引写进缓冲」那一处做，见 4.6 末尾。

---

## 六、坑清单

| # | 坑 | 现象 | 对策 |
|---|---|---|---|
| 1 | **单位换算** | 地形尺寸差 100 倍 | SkyLand 全程用米，UE 用厘米。格 2m → 200uu，层高 1m → 100uu。`terrainConfig.mjs` 里那句「地形格尺寸必须整除 chunk 尺寸」的断言要一起翻译成 `static_assert` |
| 2 | **绕序翻转** | 地形整块不可见 / 从下面才看得到 | 见决策三与 4.6 末尾。建完第一块网格先关掉背面剔除确认拓扑对，再打开 |
| 3 | **17×17 窗口** | chunk 之间一圈裂缝 | 采样窗口是 `ChunkGrid + 1` 的闭区间，不是 `ChunkGrid` |
| 4 | **符号扩展** | 高度层 -1 变成 255 | `CellHeightLevel` 必须走 `int8` 的符号扩展 |
| 5 | **浮点污染确定性** | 客户端与服务端地形不一致，玩家穿模 / 悬空 | 哈希与噪声全程整数，禁止 `FMath::PerlinNoise` / `FRandomStream` |
| 6 | **Replication Graph / Iris** | 地形 patch 同步时有时无 | **本项目当前两者都没开**（`bDisableReplicationGraph=True`，无 Iris 开关），`IsNetRelevantFor()` 正常生效。但这是**配置开关**决定的：谁把它改回 `False` 或开了 Iris，`IsNetRelevantFor()` 立刻变成死代码，chunk 相关性要改走 RepGraph 节点或 Iris filter。改动这两个开关时回头看这一行 |
| 7 | ~~**`bAutoActivate` 未设**~~ | 一个 chunk 都不生成 | ✅ 提交 `d593388` 已修。留在清单里是因为症状值得记住：invoker 没激活时 `BuildRequiredChunkSet()` 返回空集，表现是「地形代码写完了但一块都不出现」，很容易去查地形 |
| 8 | **水的语义** | 挖个坑就自动灌水 | `Water` 表示「属于已连通水域」，不是「低于海平面」。见 4.4 |
| 9 | **恒等覆盖不删** | patch store 无限增长 | 改回默认值必须删条目，见 4.5 |
| 10 | **`>= ` 写成了 `==`** 的反面 | 崖面消失、到处是缓坡 | 形状推导比较的是**恰好高一层**，见 4.3 |
| 11 | **builder 碰 `UObject`** | 无法异步，流送时卡帧 | builder 的输入保持纯数据（种子 + 覆盖格数组），见 4.7 |

---

## 七、分阶段实施步骤

### P0 — 前置修复 ✅ 已完成（结论是两条都不用改代码）

- **`bAutoActivate`**：早在提交 `d593388`（2026-08-24）就修了，
  `UOceanChunkInvokerComponent` 的构造函数里已有 `bAutoActivate = true`。
  代码审查文档写于 2026-08-18，早于那次修复。
- **复制系统**：本项目走**标准 UE 复制**——Replication Graph 与 Iris **都没启用**。
  - `Config/DefaultGame.ini` 里 `bDisableReplicationGraph=True`，
    `LyraReplicationGraph.cpp:145` 读到就 `return nullptr`，不创建 replication driver；
  - `Config/DefaultEngine.ini` 里那段 Iris 配置只是 Lyra 自带的参数表，
    全仓库搜不到 `UseIrisReplication` / `bUseIris` / `net.Iris` 的启用开关。

  所以 `AOceanChunkActor::IsNetRelevantFor()` **会被正常调用，不是死代码**，
  P4 的 patch 同步可以直接用标准相关性，不必写 Replication Graph 节点。
  这条推翻了代码审查文档的严重问题 #2，那边已加状态注记。
- 第五节的决策：一已由线稿方案 §0 拍板，三在 P1 落地时定了，
  **二（chunk 尺寸）仍未拍板**——它只影响 chunk 寻址，不影响真相层，P2 用暂定的 32×32。

### P1 — 真相层 ✅ 已完成

| 文件 | 来源 |
|---|---|
| `Public/Terrain/OceanTerrainTypes.h` | `terrainConfig.mjs` |
| `Private/Terrain/OceanTerrainHash.{h,cpp}` | `hash.mjs` |
| `Public/Terrain/OceanTerrainBiome.h` + `Private/…cpp` | `terrainBiome.mjs` |
| `Public/Terrain/OceanTerrainContent.h` + `Private/…cpp` | `terrainContent.mjs` |
| `Public/Terrain/OceanTerrainWater.h` + `Private/…cpp` | `terrainWater.mjs` + `terrainSupport.mjs` + `terrainMovement.mjs` |
| `Public/Terrain/OceanTerrainOutline.h` + `Private/…cpp` | 新增，线稿方案 §2.4，见 4.6 |
| `Private/Terrain/Tests/OceanTerrainParityTest.cpp` | 三条自动化测试 |
| `Private/Terrain/Tests/Fixtures/terrain-parity.txt` | SkyLand `scripts/export-terrain-parity.mjs` 生成 |

验收结果：两颗种子、`[-64, 64]²` 共 **33282 格地形码与 33282 格群系逐格一致**，
7 条哈希探针、10 条噪声探针全对。

**这一层不依赖引擎**（`int32` / `FVector` 这类 POD 除外），所以除了 UE 的自动化测试，
它也能在引擎外单独编译执行——P1 的比对就是这么跑出来的。这不是巧合，是 §3 那条
「底下两层不碰 `UObject`」换来的：真相层出问题时，不必起编辑器就能定位。

### P2 — 网格与碰撞（3~4 天）

- `OceanTerrainMeshBuilder.cpp` ← `terrainCollisionMesh.mjs`
- `UOceanTerrainChunkComponent`：`UDynamicMeshComponent` + trimesh `UBodySetup`
- 挂到 `AOceanChunkActor`，服务端与客户端各自构建
- 异步化：builder 进 `UE::Tasks`

产出验收：编辑器里能看到一整片台阶地形，角色能走上斜坡、被崖面挡住、chunk 边界无裂缝。

### P3 — 稀疏编辑层（2~3 天）

- `FOceanTerrainPatchStore` ← `terrainPatches.mjs`
- `FOceanTerrainEditor`（先做 `Raise` / `Lower` / `SetRamp` / `Reset`）← `terrainEditing.mjs`
- patch 变更 → 受影响 chunk 的网格重建（含跨界的相邻 chunk）

产出验收：单机下能抬高/降低一格并看到网格与碰撞同步更新，改回默认值后 store 回到空。

### P4 — 网络同步（2~3 天）

- patch 的服务端权威写入 + 增量广播
- 新玩家加入时的全量下发
- 按 P0 的结论选择 Replication Graph 节点或 Iris filter

产出验收：双客户端 PIE 下，一端编辑地形，另一端的网格与碰撞同步变化。

### 后续（不在本次范围）

- 岛屿遮罩与台阶地形的叠加（4.8 的新算法）
- 群系驱动的材质 / 脚感 / 生成偏好
- `flood` 与连通水域判定
- LOD（远处 chunk 降采样为 4m 格或直接用高度场代理）

---

## 八、验证方法

### 8.1 Parity 基准（P1 的核心产出）

这是整个迁移里最值得先做的一件事。SkyLand 的 `server/tests/terrainParity.test.mjs` 已经在做
「JS 与 WASM 逐格比对整个 code」，把它改几行就能吐出一份 JSON：

SkyLand 侧的 `scripts/export-terrain-parity.mjs` 生成
`Private/Terrain/Tests/Fixtures/terrain-parity.txt`，**只由脚本生成，不要手改**：

```
cellMin -64
cellMax 64

# hash32(seed, a, b, c) -> uint32
hash 1545219339 1 2 3 42156bc6
# valueNoise(seed, x, y, shift) -> [0, 255]
noise 1545219339 -33 17 5 144

biome 1545219339
<129 行 × 129 个四位十六进制数>
cell 1545219339
<同上，逐格完整 code>
```

格 code 恒在 `[0, 0xffff]`（高度层占高 8 位且先 `& 0xff`），所以按四位十六进制写，
文件既紧凑又能直接 diff。

**三层分开导是刻意的**：哈希翻错时先在探针那一层就红，而不是让一整张地形图无从下手；
只有群系那一格红，说明高度阈值没问题，问题在 Voronoi 或气候表。
`OceanCore.Terrain.Parity` 在哈希或噪声探针失败时直接返回——一个错的混合器会让它底下
每个值都错，报几千条下游不一致只会把唯一要紧的那一行埋掉。

**为什么必须有这个**：翻译错一个位移、一个阈值、一个盐值，现象都是
「地形看着差不多但就是对不上」。没有基准，这类错误要等到跨端不一致才暴露，
那时候的症状是玩家穿模或悬空，从现象反推到某个常量打错了几乎不可能。

### 8.2 分层测试

| 层 | 测试方式 |
|---|---|
| 哈希 | 固定输入的已知输出（从 JS 侧导几组） |
| 真相层 | 8.1 的逐格 parity |
| 网格 | 拓扑不变量：顶点去重后无孤立点、每条边最多被 2 个三角形共享、chunk 边界的顶点与邻块逐个重合 |
| 法线 | 解析法线（`SampleTerrain`）与三角形面法线在同一半边内一致，见 4.4 |
| patch | 「改回默认值后 store 回到空」、「跨界编辑会通知相邻 chunk」 |
| 同步 | 双客户端 PIE |

### 8.3 编译

```
& 'D:\Epic\Engine\UE_5.7\Engine\Build\BatchFiles\Build.bat' LyraEditor Win64 Development `
  '-Project=D:\UEPrj\OceanAdventure\LyraTemplate.uproject' `
  -WaitMutex -NoHotReloadFromIDE -NoUBA -MaxParallelActions=1
```

---

## 九、不在本方案范围内

明确划出去，避免范围蔓延：

- **SkyLand 的物件系统**（树、石头、果实、生物刷新）。`chunkContent.mjs` / `creatureSpawn.mjs`
  那一套可以之后单独评估，UE 侧用 HISM + `USpawnerComponent` 是另一条路。
- **SkyLand 的渲染风格**（线稿 / `EdgesGeometry`）。台阶地形的几何与它的美术表现是两件事。
- **Rapier 物理与角色控制器**。UE 有 Chaos 与 `UCharacterMovementComponent`。
- **`chunkStream.mjs` 的调度**。`OceanCore` 已有等价物。
- **地形编辑 UI**（`src/ui/TerrainEditorPanel.ts`、`TerrainEditController.ts`）。
  按 `AGENTS.md`，UI Widget 与输入绑定属于玩法 GameFeature 层，不进 `OceanCore`，
  且必须走 Enhanced Input + CommonUI，不能照搬 DOM 那套。

---

## 修订记录

| 日期 | 内容 |
|---|---|
| 2026-09-15 | 初版。第五节的三个决策尚未拍板，实施前需补齐。 |
| 2026-09-16 | P0 复核完成：`bAutoActivate` 早已修复；本项目未启用 Replication Graph 与 Iris，`IsNetRelevantFor()` 有效，推翻代码审查文档的严重问题 #2。 |
| 2026-09-16 | 合入 main 后跟进：决策一由 `Line-Art-Style-UE5-Mobile-Rendering.md` §0 拍板（台阶地形取代噪声高度场，服务端权威改走 UE 原生复制）；决策三的轴映射由 `(z,x,y)` 改为 `(x,z,y)`；新增 4.6 折边一节，承接线稿方案 §2.4。**P1 已完成**，parity 全绿。决策二（chunk 尺寸）仍未拍板，但它只影响 chunk 寻址，不影响真相层。 |
