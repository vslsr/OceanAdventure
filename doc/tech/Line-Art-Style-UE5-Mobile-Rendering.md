# 线稿风格在 UE5 的落地与移动端渲染优化

> 来源：同方案已存档于 SkyLand 仓库 `doc/ue5-line-art-migration.md`。
> **两份文档的定位不同**：那一份是「Web 项目如果要迁 UE 该怎么做」的评估；
> 这一份是「本项目（UE 5.7 + Lyra，已经是那个 UE 目标）要怎么改」。
> 文中凡标注 `SkyLand:` 前缀的路径都指那个仓库，是风格的**参考实现**，不是本仓库的文件。
> 相关文档：[`优化建议.md`](优化建议.md)（建造系统 CPU 侧）——那一份管 CPU，这一份管 GPU 与渲染设置。

核心主张：这套线稿风格没有一张贴图、没有一个导入资产、没有一根骨骼，
全部是程序化几何加两支手写着色器。所以「支持它」的实质不是写新东西，
而是**关掉 Lyra 模板默认打开的一整套 PBR/Lumen/TSR 设施**，再补 4 支母材质。

Lyra 的默认配置是为主机和 PC 的延迟渲染调的，与线稿 + 移动端这两个目标同时冲突。
§3 给出逐条的现状对照。

---

## 0. 本轮已落地（第 1 步）

`Plugins/LineArtCore/` —— 一个通用插件，按 `AGENTS.md` 的归属规则承载跨 GameFeature 的线稿能力。

| 文件 | 作用 |
| --- | --- |
| `Shaders/LineArtEnvironment.ush` | `environmentLighting.ts` + `createFillMaterial.ts` 的 HLSL 移植，材质用 Custom 节点 `#include` 它 |
| `Public/LineArtParameterNames.h` | MPC 参数名契约，C++ / Python / 着色器三方共用 |
| `Environment/LineArtEnvironmentSubsystem` | `applyEnvironmentInk()` 的移植：每帧算墨色、写 MPC、挑最近 4 盏点光源 |
| `Environment/LineArtPointLightComponent` | 篝火类光源的纯数据组件（不是 `UPointLightComponent`） |
| `Environment/LineArtCoreSettings` | 墨色常量与默认环境，Project Settings > Game > Line Art Core |
| `Content/Python/CreateLineArtCoreAssets.py` | 生成 MPC 与三支母材质，可重复运行 |

三条移植时**必须换算、且漏了不会报错**的量，集中在 `LineArtEnvironmentState` 与 `.ush` 的注释里：
上方向 +Y → +Z、长度 米 → 厘米、着色器从读 uniform 改为收参数。

**参数名漂移是这套结构唯一的静默故障**：材质照常编译，只是取到一个永远不更新的旧值。
所以 `ULineArtEnvironmentSubsystem::SetScalar/SetVector` 检查每一次写入的返回值，
对每个名字报一次 `LogLineArtCore` 错误。

### 本轮同时定下、但尚未动工的两条

| 决定 | 影响 |
| --- | --- |
| 地形换成 SkyLand 的 2 米方块（13 形状枚举 + 地形编辑） | `OceanCore` 的噪声高度场退役或只留远景；§2.4 的折边捷径按方块网格重算 |
| 服务端权威改用 Lyra/UE 原生复制 | 放弃「两端同一份 Rapier WASM」，6cm 和解容差那套要在 UE 侧重做 |

第二条与 §4 里「物理不要动」的建议相反：那条建议是在**保留 Web 端**的前提下写的，
既然整体换引擎，Node + Rapier 的权威就不再有留存价值。记在这里以免后来者按旧建议行事。

---

## 1. 先把风格拆成六条可移植的规则

迁移的**不是资产，是规则**。逐条附参考实现出处：

| # | 规则 | 参考实现 |
| --- | --- | --- |
| 1 | 平涂填充 + 硬边墨线两个 pass：每个物件 = 填充网格 + 按阈值角提取的轮廓线段 | `SkyLand: src/models/outlinedObject.ts` |
| 2 | 墨线是全场**共享的一支材质**，颜色是唯一调节量（WebGL 改不了线宽） | `SkyLand: src/materials/lineMaterials.ts` |
| 3 | 墨色随环境换：夜里从 `0x171614` 提亮到 `0xc4cedd`，否则纸面沉下去后轮廓消失 | 同上 `applyEnvironmentInk()` |
| 4 | 填充不是 PBR，是手写着色器：half-lambert + 半球环境光 + 云影 + 手写距离雾 + 手写点光源数组 | `SkyLand: src/materials/createFillMaterial.ts` |
| 5 | 角色墨记（腿 / 眼 / 嘴）是**另一层**：不受光、不受雾、不受色调映射，永远纯黑 | `SkyLand: src/materials/createCharacterInkMaterial.ts` |
| 6 | 全局不做的事：无阴影贴图、无 PBR/IBL、无纹理、无骨骼、无资产导入、无 LOD | 参考项目里逐条核实为零 |

**第 6 条才是移动端优化的真正抓手。** 参考项目里这份「零」清单是实测的：
阴影投射 = 0、标准 PBR 材质 = 0、蒙皮网格与动画混合器 = 0、贴图加载 = 0。
UE 默认把对应能力全打开了，把这份清单原样带过来写进项目设置，
比任何单点优化都更值钱。

**本项目的例外要先认下来**：Lyra 的角色是真骨骼动画，建造件是真导入资产。
所以第 6 条在这里是「线稿层不引入这些」，而不是「全项目为零」——
风格统一靠材质与描边，不靠把 Lyra 的角色系统拆掉。

---

## 2. 怎么画这套线稿

### 2.1 描边：三条路线，选反转外壳

| 方案 | 能画内部折边 | 移动端成本 | 与参考实现的一致度 |
| --- | --- | --- | --- |
| **反转外壳（推荐）** | ✗（只有剪影） | 低：多一个 unlit draw，无 RenderTarget 依赖 | 高，且**白送可调线宽** |
| 后处理 Sobel（Depth + Normal） | ✓ | 中高：移动 Forward 下 `SceneTexture:WorldNormal` 不可用，得自己写一张 normal RT，多一遍全屏带宽 | 中 |
| 真线段（逐边提取后画线） | ✓ | 最差：线图元批次差、对 GPU 不友好 | 最高 |

推荐组合：**主体用反转外壳，规则几何的折边用几何生成**（见 §2.4）。
落地要点和三个必踩的坑：

- **材质配置**：`Shading Model = Unlit`、`Two Sided = true`、`Blend Mode = Masked`；
  World Position Offset 输出 `VertexNormalWS * Thickness`；
  像素里判断 `TwoSidedSign > 0`（正面）时令 `Opacity Mask = 0`，等价于 cull front。
- **线宽必须屏幕空间恒定**：`Thickness = k * PixelDepth * tan(FOV/2)`。
  参考实现的墨线是固定 1 像素的，直接用世界空间厚度会让远景的线糊成一片。
- **硬边法线会把外壳撑裂**：low-poly 硬边模型沿法线挤出会在每条折边处开口。
  必须在 Blender 侧把**平均法线烘进 vertex color / UV2**，材质里读它作为挤出方向。
  **这是这套风格最容易翻车的一步**，且它落在建模管线里——
  按 `AGENTS.md`，相关脚本归 `blender/script/python/`，要与
  `Naval-Buildable-Module-Art-Spec.md` 的包络规范一起改。
- 墨色接 Material Parameter Collection（见 §2.2），一处写、全场生效。

### 2.2 填充：一支 Unlit 母材质

不要用 `Default Lit`。参考实现的片元着色器是自洽的——half-lambert、半球染色、
云影、方向性散射雾、点光源数组——在 UE 里就是一个 **Unlit 母材质**，结果输出到 Emissive。

- **最大的一笔省**：移动端因此跳过整个光照 pass、shadow pass、反射捕捉与天光。
- 单色与逐顶点染色两种取色方式 → **Per-Instance Custom Data** 或 Vertex Color。
  一个母材质 + 一个 PSO 覆盖整个世界。
- 所有环境量（墨色染色、日照强度、太阳方向、天顶染色、云影偏移、雾参数）
  → 一个 **Material Parameter Collection**。天气 / 昼夜系统每帧写 MPC，零 draw call 代价，
  与参考实现里「场景级共享 uniform」是同一个设计。
- **火把 / 篝火那类点光源继续手写**：取离视点最近的几盏，塞进 MPC 的向量数组。
  不要用动态点光源——移动端每盏都是实打实的开销，而这里只需要一个衰减球。
- 角色墨记层：Unlit + `Disable Depth Test` + 不接雾节点 + 不参与色调映射。

**材质放哪里**：线稿母材质是跨 GameFeature 共享的，按 `AGENTS.md` 的归属规则
必须下沉到通用插件的 `Content/`（`OceanCore` 或 `NavalCore`，两者都已开
`CanContainContent`），**不能**放进 `Plugins/GameFeatures/<Feature>/`——
否则 Raft 与 OceanAdventure 会各存一份，画风迟早分叉。
先例是共用的那门炮 `/NavalCore/Blueprints/Cannon/BP_Naval_Cannon`。

### 2.3 材质总数控制在 4 支

`fill` / `outline` / `character-ink` / `water + 植被的 WPO 变体`。

移动端的隐形杀手是 **shader permutation 数量与 PSO 编译卡顿**。
母材质越少，打包出的 PSO cache 越小、首次遇到新材质时的掉帧越少。

### 2.4 规则几何的轮廓白拿

参考项目的地形是 13 项形状的封闭枚举，平面与斜坡的交界在枚举里已知，
**不需要跑拓扑边提取**。

本项目的对应物是建造模块：`200 × 200 × 150 cm` 的固定包络、固定吸附单元
（见 `Naval-Buildable-Module-Art-Spec.md`）。模块的边同样是**设计时已知的**，
可以在建模阶段就输出一条边条带，比反转外壳更准也更省。
船体与岛屿地形能不能吃到同一条捷径，取决于 `OceanCore` 的网格是不是规则网格——
这一条需要在第 2 步落地时实测确认。

---

## 3. 移动端：从 Lyra 默认值开始的「关掉什么」清单

### 3.1 现状对照（`Config/DefaultEngine.ini` · `[/Script/Engine.RendererSettings]`）

下面每一项都是仓库里的**当前值**，全部与本方案冲突：

| 现状 | 目标 | 理由 |
| --- | --- | --- |
| `r.AntiAliasingMethod=4`（TSR） | **MSAA**（Forward 下）| 见 §3.2，TSR 会糊掉并抖动 1px 墨线 |
| `r.DefaultFeature.MotionBlur=True` | `False` | 线稿不要运动模糊 |
| `r.DynamicGlobalIlluminationMethod=1`（Lumen） | `0` | Unlit 填充用不到 GI |
| `r.ReflectionMethod=1`（Lumen） | `0` | 同上 |
| `r.Lumen.HardwareRayTracing=True` / `r.RayTracing=True` | `False` | 移动端不可用 |
| `r.Shadow.Virtual.Enable=1` | `0` | 零阴影风格，改用接触阴影贴片 |
| `r.VirtualTextures=True` | `False` | 零贴图 |
| `r.GenerateMeshDistanceFields=True` | `False` | 无 DF 阴影 / 无 DF AO 需求，省烘焙与内存 |
| `r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=True` | 曝光固定 | 见 §3.3，曝光会把纯黑抬成深灰 |

已经对路、不用动的两条：`r.AllowStaticLighting=False`、`r.Mobile.EnableStaticAndCSMShadowReceivers=False`。

> 改这些要连 `Config/DefaultDeviceProfiles.ini` 与 Lyra 自己的画质档
> （`PerformanceSettings` / 可扩展性档位）一起过一遍，否则运行时会被设备档覆写回去。

### 3.2 抗锯齿：TSR / TAA 是线稿的敌人

**用 MSAA，不要 TSR / TAA。** 两条理由都致命：

1. 时域抗锯齿会把 1–2 像素的墨线糊掉，并在相机移动时让细线**抖动、闪烁**——
   线稿最显眼的地方恰好在这里。
2. Forward + MSAA 在移动端 TBDR 架构上是 tile 内解析的，带宽代价远低于桌面直觉，
   而它解决的正是「硬边锯齿」这个本风格的核心痛点。

FXAA 同样不要用：它会把墨线当成锯齿抹平。

**代价要认下来**：换掉 TSR 等于放弃它的上采样，移动端得原生分辨率渲染。
这一条要在第 4 步的真机体检里量，不能想当然。

### 3.3 色彩：必须关掉色调映射

参考实现里那句「不参与色调映射」的注释写明了原因：**曝光调整会把纯黑抬成深灰**。
UE 默认的 ACES 色调映射 + 自动曝光会同时毁掉纸面的浅色和墨线的黑。

- Post Process Volume：曝光固定（Min = Max）；关 Bloom（或压到 0.05 以下）、
  Motion Blur、Vignette、Chromatic Aberration、Lens Flare。
- 色调映射：用 Replace Tonemapper 后处理材质走 pass-through，或把 Film 曲线设成中性。
  要的是**所见即所得的平涂色**。
- `Mobile HDR`：若不需要 bloom 与 HDR 昼夜过渡，关掉可省一整遍带宽；
  保留也无妨——昼夜染色本身是 MPC 乘色，LDR 路径一样做得到。

### 3.4 Draw call：按种类实例化

- 同一类物件（模块、植被、岩石…）→ **每类一个 HISM**，
  加上 fill + outline 两 pass = 每类 2 个 draw。
- 颜色走 **Per-Instance Custom Data**，不要为了换色拆组件。
- 本项目已经在走这条路：`UBuildStructureVisualComponent` 就是 ISM 渲染建造件。
  但它现在 `ClearInstances()` 后重加全部实例——描边 pass 会让这个开销**翻倍**，
  所以 `优化建议.md` §1.2 的增量更新是本方案的**前置项**，不是可选优化。
- 植被：移动端**建议放弃草的独立 outline pass**。每片草叶两个 batch 太贵；
  把墨边做进草叶几何（一个深色窄三角）或在材质里按 UV 画。视觉差异极小，成本减半。
- 目标：移动端 **< 250 draw call、< 500k 三角面 / 帧**。

### 3.5 PSO 与启动卡顿

把「关掉的东西」写进项目级配置（而不是只在关卡里关），
能同时砍掉 shader permutation 数量、包体和 PSO 数量。

上线前跑一遍 **bundled PSO cache 收集**：移动端最常见的「偶发掉帧」
是首次遇到新材质时的 PSO 编译，不是渲染负载。

---

## 4. 逐系统映射

| 参考实现（SkyLand） | 本项目落点 |
| --- | --- |
| 填充材质的手写着色器 | Unlit 母材质 + Material Parameter Collection，放通用插件 `Content/` |
| 填充 + 轮廓两 pass | HISM × 2（fill + 反转外壳） |
| 共享墨线材质 + 随环境换墨 | MPC 的墨色参数，天气 / 昼夜系统每帧写一次 |
| 角色墨记材质 | Unlit + Disable Depth Test + 不接雾 / 不参与色调映射 |
| 程序化软体形变 | World Position Offset；Lyra 角色保留骨骼，不强行改 |
| 线稿火焰 | Niagara + 同一支 unlit 线稿材质 |
| 手写距离雾 | 材质内手写雾，**不要** Exponential Height Fog（走的是贵的那条路） |
| 海面 | `OceanCore` 现有网格 + WPO，材质换成线稿母材质 |

---

## 5. 落地顺序（每步都能看到画面）

1. **一个球 + 一个建造模块**：跑通 Unlit 母材质 + 反转外壳 + MPC 换墨。
   先把「硬边法线撑裂外壳」和「屏幕空间线宽」这两个坑填了，后面才有意义。
2. **一条建满的船**：建造件走 HISM × 2，量 draw call 与帧时间；
   同时确认 `优化建议.md` §1.2 的增量更新已经就位。
3. **环境**：昼夜染色、云影、雾、点光源数组，与参考实现逐帧比色。
4. **移动端体检**：真机上看渲染统计，确认 MSAA 开、TSR 关、色调映射中性、
   PSO cache 已收集；同时量 §3.2 里放弃上采样的代价。

---

## 6. 一件必须重新做决定的事

WebGL 改不了线宽，所以参考实现的整套美术是围绕「1 像素固定墨线、只调墨色」长出来的——
`SkyLand: src/materials/lineMaterials.ts` 开头那段注释把这个前提写得很清楚：
**线宽在 WebGL 里改不了，所以墨色是线稿唯一的调节量。**

UE 给了真正可变的线宽。但如果一上来就放开它，那套靠墨色浓淡撑起来的夜景与雾景层次会整个失衡
（夜里把墨提亮、网格让出不透明度，这些补偿都是在「线宽恒定」的前提下调出来的）。

**建议第一版把线宽锁死成屏幕空间恒定值**，先复刻参考实现的手感；
等画面稳定后，再决定要不要动它。
