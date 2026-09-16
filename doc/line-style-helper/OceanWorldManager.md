编译通过了。给你清单之前先确认几件事——我不想让你按我猜的步骤去点。

确认完了,有个**卡点得先说**:现在没有任何东西把地形组件挂到 chunk actor 上。组件是自接线的(找 owner、订阅 `OnChunkInitialized`),但它得先存在于那个类上。所以编辑器里第一步不是看画面,是挂组件。

## 顺序

**第 0 步 · 挂组件**

`Plugins/GameFeatures/OceanAdventure/Content/Blueprints/BP_OceanWorldManager.uasset` 上看 `ChunkClass` 指向谁(大概率是 `BP_OceanChunk_Debug`)。打开那个类 → Add Component → 搜 **Ocean Terrain Chunk** → 加上。不用连任何蓝图节点,它自己会订阅。

**第 1 步 · 先跑自动化测试(最便宜,不用开地图)**

`Window → Developer Tools → Session Frontend → Automation`,过滤 `OceanCore.Terrain`,应该有 5 条:

| 测试 | 它保的东西 |
|---|---|
| `Parity` | 33282 格地形码 + 33282 格群系逐格对上参考实现 |
| `CellCode` | 位打包、出生区平坦、采样高度与角点一致、水体语义 |
| `Outline` | 折边判定 |
| `Mesh` | 顶面投影面积精确平铺、法线、接缝包含性 |
| `Patches` | 编辑语义、通知集合 == 实际变化集合 |

这 5 条我在引擎外全跑过并且全绿。**如果这里就红了,说明是 UE 侧接线问题(夹具路径、`IPluginManager` 找不到插件),不是算法问题**——算法已经逐格对过了。

**第 2 步 · 开 `L_OceanChunkTest`,Play**

## 怎么算通过

按这个顺序看,每条失败的含义不一样:

| 现象 | 含义 | 我会改哪里 |
|---|---|---|
| Output Log 有 `ChunkSize ... terrain grid is 32 cells` 的 Error | `BP_OceanWorldManager` 或 DataAsset 里手填了 20000,绕过了编译期默认值 | 把那个值清掉让它回默认 |
| **地形完全看不见,或只有从地下往上看才看得到** | **绕序反了** | `OceanTerrainMeshBuilder.cpp` 的 `AppendTriangle` 里翻一次,只此一处 |
| 世界原点有一块 **22m×22m 的平地**,再往外才起伏 | ✅ 真相层确实在驱动渲染(出生安全区是生成器的硬保证) | — |
| chunk 之间有裂缝 | 采样窗口出问题 | 但 `Mesh` 测试应该先红,先看它 |
| 地形出来了但**全是灰色** | ✅ 正常,不是 bug | 见下面「材质」 |
| 角色走不上斜坡 / 穿过地面 | 碰撞没建成 | `EnableComplexAsSimpleCollision` + `UpdateCollision` 那一段 |
| 崖面挡不住人 | 同上 | 同上 |

**材质**:`M_LineArt_Fill` 现在还不存在——`CreateLineArtCoreAssets.py` 生成它,而 `Plugins/LineArtCore/Content/` 里目前只有那个脚本本身。所以地形默认是灰的。跑完那个脚本后,把 `M_LineArt_Fill` 填进地形组件的 `FillMaterial`,逐顶点的群系色才会出来。**灰色不影响几何验收**,前面那些都能在灰模上看。

**墨线**:勾组件的 `bDrawInk` 能看到折边/崖线(崖线红、折边黑)。⚠️ 有个坑是我写的:tick 只在构建完成时按 `bDrawInk` 开关,所以**运行中临时勾它不生效**,得开 Play 之前就勾上。这条我可以改成即时生效。

## 最后一步:编辑(P3)

这一条现在**没有便利入口**。要验证「抬高一格 → 网格与碰撞同步更新,而且边界格会带着邻块一起更新」,得自己拉个蓝图调 `UOceanTerrainSubsystem` 的 `Raise Cell` / `Lower Cell` / `Reset Cell`。

我可以加一个 exec 控制台命令(`Ocean.Terrain.Raise X Y` 之类),这样你直接在 `~` 里敲就行,不用画蓝图。要的话我现在加,但你得再编一次——你看是现在加,还是先把前面几步走完、确认几何和碰撞没问题再说?