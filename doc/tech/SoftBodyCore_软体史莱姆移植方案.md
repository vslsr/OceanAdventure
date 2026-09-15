# 软体史莱姆移植方案（SkyLand → OceanAdventure）

> 目标：把 SkyLand（H5 / TypeScript）里那只**软体外壳史莱姆**（`pbf-slime`）复刻到本工程，
> 成为一个通用框架插件 `SoftBodyCore` + `OceanAdventure` 玩法层的可复用生物。
>
> 生成日期：2026-09-15
> 适用引擎：UE 5.7 + Lyra
> 源实现：`SkyLand` 仓库 `src/slime/hybrid/`、`src/models/actors/createPbfSlimeModel.ts`、
> `shared/softBodyDeformation.mjs`、`config/actors/pbf-slime.actor.json`
> 范围：只含软体外壳那只。带骨骼腿的 `legged-slime` 复用本文第 3～5 节的外壳部分，步态另立文档。

---

## 目录

- [一、方案总览](#一方案总览)
- [二、源实现解剖](#二源实现解剖)
  - [2.1 五层结构与归属](#21-五层结构与归属)
  - [2.2 外壳求解器是什么](#22-外壳求解器是什么)
  - [2.3 求解器的公开入口](#23-求解器的公开入口)
- [三、UE 侧技术选型](#三ue-侧技术选型)
- [四、目录结构与模块分层](#四目录结构与模块分层)
- [五、坐标、单位与三个静默坑](#五坐标单位与三个静默坑)
- [六、逐子系统落地](#六逐子系统落地)
- [七、网络契约](#七网络契约)
- [八、数值资产映射](#八数值资产映射)
- [九、性能、LOD 与休眠](#九性能lod-与休眠)
- [十、分阶段任务与验收](#十分阶段任务与验收)
- [十一、自动化测试](#十一自动化测试)
- [十二、待定问题](#十二待定问题)

---

## 一、方案总览

### 核心决策

| 决策项 | 选择 | 理由 |
|---|---|---|
| 软体实现 | **移植原求解器为纯 C++**，不用引擎现成软体 | 手感全在那几十个调过的常数里；Chaos Flesh / Deformer Graph 是 GPU 侧、读不回来、不参与网络，也没法按命中点精确塑形 |
| 网格载体 | `UDynamicMeshComponent`（GeometryFramework） | 每帧只改 425 个顶点位置；比重建 `ProceduralMeshComponent` 的 section 便宜且语义正确 |
| 插件归属 | 新建通用框架插件 `Plugins/SoftBodyCore/` | 求解器不碰 GAS、不碰 `LyraGame`；作弊命令、编辑器预览、自动化测试要能直接调（`AGENTS.md` 分层判据） |
| 玩法归属 | `Plugins/GameFeatures/OceanAdventure/` | GAS 能力、输入、AI、DataAsset、BP、材质 |
| 形变是否复制 | **否。外壳 425 个顶点一个字节都不过网络** | 形变是纯表现；过网络的只有"谁在哪一点拉/咬"的几个浮点数 |
| 玩家拖拽上行 | **GAS TargetData** | `AGENTS.md`：不得在组件上自造 `Server`/`Client` RPC |
| 受击凹陷 | **GameplayCue** | 伤害在服务端结算，凹陷是表现；Cue 参数带来袭方向与力度 |

### 一句话

> 这只史莱姆的价值不在"一个会抖的球"，而在**"权威只有一个胶囊 + 几个浮点数，其余全是客户端各自解算"**这条分界线。
> 移植时唯一不能破的规矩就是别把它做成复制数据。

---

## 二、源实现解剖

### 2.1 五层结构与归属

`config/actors/pbf-slime.actor.json` 看起来是一个 Actor，实际叠了五层：

| 层 | 源文件 | 权威状态？ | 过网络的数据量 |
|---|---|---|---|
| 碰撞 / 移动 | `playerMovement` + 胶囊（`collisionRadius 0.52` / `collisionHeight 0.72`），**软体不参与碰撞** | 是 | 常规 transform |
| 软体外壳 | `src/slime/hybrid/HybridSlimeSimulation.ts`（1266 行） | 否 | 0 |
| 体积流动 | `src/slime/hybrid/HybridSlimeVolumeFlow.ts` | 否 | 0 |
| 外力形变（拖拽 / 被咬） | `shared/softBodyDeformation.mjs` + `SoftBodyDeformationComponent` | 命中点是 | 拖拽 6 个 float；每张嘴 3 个 float，最多 3 张 |
| 装饰层 | `createPbfSlimeModel.ts`：核心球、9 个气泡、眼睛、接触阴影 | 否 | 0 |

### 2.2 外壳求解器是什么

**不是 PBF（尽管模型 id 叫 pbf），也不是有限元**。它是一张固定拓扑的球面蒙皮（`SphereGeometry(1, 24, 16)` → **425 个顶点**），
每个顶点受三项力，外加一层全局体积耦合：

| 项 | 作用 | 参数 |
|---|---|---|
| 锚点胡克弹簧 | 每个顶点回到"静止形状上属于自己的那一点" | `skinStiffness` |
| 邻域拉普拉斯耦合 | 顶点向邻居的平均位移靠拢，皮不会裂开 | `neighborStiffness`，邻域 = 球面约 20°（`dot ≥ 0.94`）内的顶点，**启动时预计算一次** |
| 核心弹簧 | 整团质心追 Actor 根，阻尼 `2√k × 0.96`（接近临界） | `coreStiffness`，滞后目标 = `-速度 × 0.06s`，夹在 `0.22r` |
| 体积流动 | 局部弹簧看不见全局缺料；把丢失/多出的体积按重力权重重分配到锚点 | `FILL_GAIN 0.62`、`RESPONSE_RATE 6.5`、42% 沿重力下坠、单顶点补偿上限 `0.24r` |

静止形状不是球：上半球是穹顶、下半球压成贴地软底
（`HYBRID_SLIME_CENTER_HEIGHT_RATIO 0.46`、`PLANAR_RADIUS_RATIO 0.82`、`VERTICAL_RADIUS_RATIO 0.44`、
`FLOOR_HEIGHT_RATIO 0.018`、边缘下垂指数 `1.28`）。**这套静止形状必须原样移植**——
它决定了这只史莱姆"瘫在地上"而不是"悬空的椭球"。

积分：固定步 **1/120 s**，单帧最多 0.1 s（超出的丢掉，不做追帧），显式欧拉 + 指数阻尼。
地面：顶点被夹在 `floorY` 之上；离地权重内（`0.2r`）水平速度按 `exp(-12 × w × dt)` 额外阻尼，模拟黏地；
起跳时地面约束按 `airborneAmount` 释放到 `1.1r`。

休眠：连续 `0.08s` 满足全部稳定判据（最大蒙皮误差 `< 0.0015r`、动能 `< 2.5e-5 r²`、核心误差 `< 0.001r`…）
就把顶点吸附回锚点并停止解算。**一群静止的史莱姆必须是零成本**，这条要移植。

### 2.3 求解器的公开入口

移植后应逐一对应成 `FSoftBodyShellSolver` 的成员函数：

| 源 API | 语义 | 备注 |
|---|---|---|
| `setDriveVelocity(vx, vz)` | 水平驱动速度 → 核心滞后 + 水滴形变方向 | 玩法侧每帧喂 |
| `setAirborneMotion(vy, grounded)` | 起跳/下落：核心反向惯性、拉长、起跳脉冲 | 起跳那一帧直接注入速度，不能淡入 |
| `applyCollision(dx, dz, dt)` | 撞墙：只压入迎面那一侧，凹深 `0.13r + min(0.22, 速度×0.065)` | 一次性事件 |
| `applyProjectileImpact(dir, impulse)` | 中箭：沿弹道给迎面一侧冲量，权重 `facing^6`，凹深 `0.3r`、速度 `3.4r` | **冲量，不是状态** |
| `beginSurfaceDrag / setSurfaceDragPull / endSurfaceDrag` | 鼠标拖拽：吸附到最近顶点，smoothstep 影响圈 + 全局跟随下限 `0.45`，底部黏地系数 `0.45` | 位移先硬截断再按伸长比衰减拉力 |
| `setBiteTips(tips)` | 被咬：**静止外形的一项**（按顶点方向与轴夹角 `^8` 连续长出尖），不是一块被抓的皮 | 所以没有影响圈边界、不会裂 |
| `setDeathCollapse(amount)` | 死亡摊开：横向 `1.4` 倍、压到 `0.25`、阻尼 `×1.6` | 0→1 过渡 |
| `update(dt) -> bool` | 返回本帧是否变化，调用方据此决定要不要重算法线 | 休眠时直接返回 false |

---

## 三、UE 侧技术选型

已评估并**排除**的方案：

| 方案 | 为什么不用 |
|---|---|
| Chaos Flesh（软体解算器） | 面向布娃娃/肌肉的 GPU 解算，不复制、参数与本文这套手感常数不可迁移，且无法按"命中点 + 位移"精确塑形 |
| Deformer Graph（GPU 形变） | 结果读不回 CPU：死亡摊开后的轮廓、命中点吸附、体积误差统计都要在 CPU 侧可查 |
| Morph Target / 动画序列 | 形变是连续外力驱动的，不是有限个姿势 |
| 材质 WPO 逼近 | 单独用做不出"被拉出一个尖"；但**适合做远距离 LOD**，见第九节 |
| 骨骼蒙皮（一堆骨头绑球） | 425 顶点的解算成本本来就低于维护一套骨骼映射 |

选定：**CPU 求解器 + `UDynamicMeshComponent`**。425 顶点 × 120Hz 的浮点量级在单只上完全可忽略，
成本与世界大小、Actor 数量无关（源实现刻意保证的性质）。

---

## 四、目录结构与模块分层

```
Plugins/SoftBodyCore/                       ← 通用框架层：只依赖 Engine + GeometryFramework
  SoftBodyCore.uplugin                      ← CanContainContent = true（要装基础材质）
  Source/SoftBodyCoreRuntime/
    Public/
      SoftBodyShellSolver.h                 ← 纯 C++，零 UObject；HybridSlimeSimulation 的移植
      SoftBodyVolumeFlow.h                  ← HybridSlimeVolumeFlow 的移植
      SoftBodyRestShape.h                   ← 静止形状的四个比例 + 边缘下垂
      SoftBodyShellComponent.h              ← UDynamicMeshComponent 子类：喂输入、写顶点
      SoftBodyGripState.h                   ← 命中点净化/换算（softBodyDeformation.mjs）
      SoftBodyGripComponent.h               ← 复制层：最多 3 个抓取者的 FastArray
    Private/
      Tests/                                ← FAutomationTest：与源实现逐步对数
  Content/
      M_SoftBodyShell_Unlit                 ← 通用无光照半透外壳母材质

Plugins/GameFeatures/OceanAdventure/
  Source/OceanAdventureRuntime/{Public,Private}/Creature/
      SlimeCreature.h/.cpp                  ← Pawn/Character：装配外壳、抓取、AI
      SlimeDefinition.h/.cpp                ← UPrimaryDataAsset，对应 pbf-slime.actor.json
      GA_SlimeGrab.h/.cpp                   ← 玩家拖拽能力（TargetData 上行）
      GC_SlimeImpact.h/.cpp                 ← 受击 GameplayCue
  Content/Creature/
      BP_Slime、MI_Slime_*、DA_Slime_Green、AbilitySet、StateTree、实验用小地图
```

依赖方向（单向，符合 `AGENTS.md` 与 `doc/tech/Plugins-Overview.md`）：

```
OceanAdventure (GameFeature)  ──依赖──▶  SoftBodyCore (通用框架)  ──依赖──▶  Engine
```

`SoftBodyCore` **不得**出现 `LyraGame` / `GameplayAbilities` / `CommonUI` / 任何 GameFeature 的依赖。
外壳组件通过 `GameFeatureAction_AddComponents` 注入还是作为 BP 的默认子对象，按判据取后者：
**它构成这只生物的本体，离开它这个 Actor 没有意义**。抓取组件（`USoftBodyGripComponent`）则按能力性组件注入。

---

## 五、坐标、单位与三个静默坑

| 源 | 本工程 | 换算 |
|---|---|---|
| 米 | 厘米 | 所有长度 **×100**（`radius 0.95 m` → **95 cm**） |
| Y-up 右手 | Z-up 左手 | `(x, y, z)_src → (x, z, y)_ue`，并翻一个轴的符号；只在**装配边界**换一次，求解器内部保持源坐标语义 |
| 刚度（1/s²） | 同 | **不随尺度变，直接抄** |
| 速度类常数 | 同 | ×100（`IMPACT_SPEED_RADIUS_RATIO` 本身按半径缩放，不用动） |

### 坑 1：外壳不跟着 Actor 的 yaw 转（最贵的一个）

源实现的 `worldToShellOffset` **只减原点、不转 yaw**，注释里专门写了这是"类型检查和单元测试都拦不住"的一处：
拿转过 yaw 的本地坐标算形变，两只史莱姆面对面时，咬出来的尖会偏 180° 从**背面**冒出来。

落地要求：`USoftBodyShellComponent` 挂在一个 `SetUsingAbsoluteRotation(true)` 的场景组件下，
所有外力入口（`ApplyProjectileImpact`、`SetBiteTips`、拖拽命中点）接收的都是**世界轴向**向量，只减原点。
自动化测试里放一条断言：Actor yaw = 180° 时，同一个世界命中点算出的突起向量不变。

### 坑 2：受击的"整只后仰"要用水平分量

来袭轴是三维的（吊射的箭该在迎箭侧**上方**开坑），但"整只往后仰"只跟水平方位有关。
源实现为此单独留了一个水平单位向量。合成一个轴的话，越陡的箭仰得越少——实际正好相反。

### 坑 3：半透明材质不许关深度测试

源实现的眼睛开了 `depthTest: false`。本工程 `AGENTS.md` 明令禁止（会盖住整个场景）。
改用 **CustomDepth 模板**或朝相机方向的微小 Z 抬升实现"墨记永远在最前"，
层序用 `TranslucencySortPriority` 排：外壳 5 > 气泡 3 > 核心 2 > 接触阴影 1。

---

## 六、逐子系统落地

### 6.1 `USoftBodyShellComponent`

- **构造**：按 `USlimeDefinition` 生成 24×16 UV 球，缓存 `SurfaceDirections`（单位方向）与 `SurfaceNeighbors`
  （`dot ≥ 0.94` 的邻接表，**构造时算一次**，425² 的双重循环只跑一遍）。
- **Tick**：累加器 → 固定 1/120 子步 → 求解 → 写 `FDynamicMesh3` 顶点 → 只在 `update()` 返回 true 时
  重算法线并 `NotifyMeshUpdated()`。休眠时整个 Tick 跳过（`SetComponentTickEnabled(false)`，由外力入口唤醒）。
- **输入**：每帧从宿主 Pawn 取水平速度 / 竖直速度 / `IsMovingOnGround()`，喂 `SetDriveVelocity` / `SetAirborneMotion`。
- **碰撞凹陷**：用 `UCharacterMovementComponent` 本帧的 `GetLastUpdateVelocity() - Velocity` 或 hit 法线推出
  "本帧试图进入障碍的方向"，喂 `ApplyCollision`。外壳半径比玩法胶囊大约 0.27r，
  不接这一步的话玩法碰撞是对的、画面上却像硬壳插进墙里。

### 6.2 拖拽（玩家把史莱姆的皮拉出一个尖）

1. 客户端 `UGA_SlimeGrab` 激活：对外壳做射线检测取命中点 → 转成**外壳坐标**（世界轴向、只减原点）。
2. 每帧（节流到 20Hz）用 `CallServerSetReplicatedTargetData` 上报 `FVector Contact + FVector Pull`（6 个 float）。
3. 服务端在同一能力里用**同一个校验函数**复检：`FSoftBodyGripState::Sanitize()`——
   任一分量非法整体作废、夹到 ±400 cm、`600 ms` 无新上报判定松手、命中点移动超过 `2 cm` 算重新抓取。
4. 服务端写进史莱姆身上的 `USoftBodyGripComponent`（`FFastArraySerializer`，最多 `MAX_SOFT_BODY_HOLDERS = 3` 项），
   `MarkItemDirty` + `ForceNetUpdate`；复制回调只标脏，重建合并到下一帧。
5. 所有客户端把收到的 6 个数喂给本地求解器。位置是连续量，两帧之间直接插值，**不需要 revision**。

### 6.3 被咬 / 被抓（其它 Actor 施加的外力）

突起向量 = `身体中心 → 抓握点`，长度 `max(gripDepth, 距离 - 半径)`（源 `resolveGripTip`）。
它是"静止外形的一项"而不是一块被抓住的皮，所以施力方绕到另一侧时方向自己会转，
不需要重新抓取，也不会在影响圈边缘裂开。上限 `1.15r`，超过 `breakDistance` 拉断。
这一层复用 6.2 的同一个 FastArray（抓取者可以是玩家指针，也可以是别的生物的嘴）。

### 6.4 受击与死亡

- 伤害走 Lyra 既有链路（服务端结算）。
- 表现：`GameplayCueNotify_Burst`，Cue 参数带 `Normal`（弹道方向，世界轴向）+ `RawMagnitude`（0~1 力度）
  → `ShellComponent->ApplyProjectileImpact()`。**不复制凹陷本身**。
- 死亡：在死亡能力/Cue 里把 `DeathCollapse` 从 0 线性推到 1（横向铺到 1.4、压到 0.25、蒙皮阻尼 ×1.6），
  然后停 Tick，尸体保持摊开的形状。

### 6.5 装饰层

核心球（`0.3r`，缓慢漂移 + 呼吸）、9 个气泡（沿黄金比相位上浮，`0.5 + 0.5·sin(πt)` 缩放）、
两只眼睛（`±0.23r`，Y 向 1.18 拉长）、接触阴影（圆片，浓度随挤压变化）。
这些在 UE 里就是几个静态网格 + 一个材质参数集合，由外壳组件每帧写 `SetVectorParameterValueOnMaterials`
或直接设相对位置。**它们不进求解器**。

### 6.6 材质

外壳：`Unlit` + `Translucent` + 双面 + 关深度写入。源配色（`pbf-slime.actor.json`）：
`surfaceColor #90ebcb`（不透明度 0.45）、`innerColor #3ca98e`、`highlightColor #d8fff0`、
`bubbleColor #e8fff8`、`shadowColor #7bd3bd`、`inkColor #000000`。
本工程不是无光照线稿风，所以这组颜色应作为 `MI_` 的初值，交给美术在本工程光照下重新定；
**结构（层序、双面、不写深度）不能改，颜色可以**。

---

## 七、网络契约

每只史莱姆每次复制的形变数据上限：

| 内容 | 大小 |
|---|---|
| 拖拽命中点 + 位移 | 6 × float（可量化到 16 位，±400 cm 精度 1.2 cm） |
| 抓取者（最多 3） | 每个 3 × float 的突起向量 + 一个 id |
| 合计 | **< 40 字节**，且只在有人拉/咬时才有 |

外壳 425 个顶点、体积流补偿、法线：**全部为 0**。

规矩（照抄 `AGENTS.md`）：
- 客户端上来的一律视为请求，服务端用同一个校验函数完整复检；
- 状态真值只服务端写，客户端只在表现层预测（拖拽可本地立即起形变，服务端拒绝后自然超时归位）；
- 失败与事件反馈用 `UGameplayMessageSubsystem` 广播，不要 Client RPC 单播；
- FastArray 写后 `MarkItemDirty`、删后 `MarkArrayDirty`，再 `ForceNetUpdate`（改过休眠还要 `FlushNetDormancy`）。

---

## 八、数值资产映射

`USlimeDefinition : UPrimaryDataAsset`（**不许把这些硬编码进 C++**）：

| 源字段（米） | DataAsset 字段（厘米） | 值 | 说明 |
|---|---|---|---|
| `render.radius 0.95` | `ShellRadius` | 95 | 外壳半径 |
| `collisionRadius 0.52` / `collisionHeight 0.72` | `CapsuleRadius` / `CapsuleHalfHeight` | 52 / 36 | **玩法碰撞用它，不是外壳** |
| `centerForce 22` | `CoreStiffness` = `max(18, ×2.4)` | 52.8 | 核心弹簧 |
| 同上 | `SkinStiffness` = `max(28, ×2.8)` | 61.6 | 锚点弹簧 |
| 同上 | `NeighborStiffness` = `max(7, ×0.9)` | 19.8 | 邻域耦合 |
| `viscosity 10` | `SkinDamping` = `max(8, ×1.4)` | 14 | 蒙皮阻尼 |
| `bubbleCount 9` | `BubbleCount` | 9 | |
| `slimeSurfaceDrag.maximumDistance 1.05` | `GrabMaxDistance` | 105 | 拉到多长截断 |
| `.pullForce 120` | `GrabPullForce` | 120 | 刚度量纲，不换算 |
| `.falloffExponent 1.35` / `.influenceRadius 1.15` | `GrabFalloff` / `GrabInfluenceRadius` | 1.35 / 115 | |
| `bite.gripDepth 0.35` | `GripMinimumDepth` | 35 | 贴身咬的保底尖长 |
| `softBodyDeformation.breakDistance 3.0` | `GripBreakDistance` | 300 | 拉断距离 |
| `.selfReportTimeoutMs 600` | `GrabTimeout` | 0.6 s | |
| `health.maximum 100` | 走 Lyra 属性集 | 100 | |
| `playerMovement.walkSpeed 3.2` 等 | 走 `UCharacterMovementComponent` | — | 不要另起一套移动 |

求解器内部那些比例常数（`0.46 / 0.82 / 0.44 / 0.018 / 1.28`、`FILL_GAIN 0.62`、`RESPONSE_RATE 6.5`、
`IMPACT_FALLOFF_EXPONENT 6`、`BITE_TIP_EXPONENT 8`…）**不进 DataAsset**：它们是这套手感的定义，
暴露出去只会被随手调坏。要调的话改代码并同步本文档。

---

## 九、性能、LOD 与休眠

| 档位 | 触发 | 内容 |
|---|---|---|
| L0 | 近处 / 正在被拉被咬 | 完整求解，1/120 固定步，体积流开 |
| L1 | 中距 | 固定步降到 1/60，体积流关（静止形状 + 弹簧仍在） |
| L2 | 远距 | 停求解，换材质 WPO：只做挤压/摇摆/呼吸三条公式曲线 |
| 休眠 | 稳定 0.08 s | 顶点吸附回锚点，关 Tick；任何外力入口唤醒 |

三条硬性性质（源实现刻意保证，移植后要保住）：
1. 单只成本与世界大小、Actor 数量**无关**；
2. 静止时体积误差为 0，补偿量衰减到 0，**不会阻止休眠**；
3. 每帧工作量是常数，不随形变量增长。

---

## 十、分阶段任务与验收

每一步可单独合入、单独回滚。

| 阶段 | 内容 | 验收 |
|---|---|---|
| **0** | `SoftBodyCore` 插件骨架 + 求解器与体积流的纯 C++ 移植（零 UObject） | `FAutomationTest`：静止体积误差→0、能休眠、单步结果与源实现对齐（见第十一节） |
| **1** | `USoftBodyShellComponent` + DynamicMesh；一只只会呼吸的史莱姆 | PIE 里看得见静止形状是"瘫着"的、会呼吸、会睡着（`stat` 里 Tick 归零） |
| **2** | 接到 Pawn：走路挤压、起跳拉长、落地压扁、撞墙凹陷 | 走/跳/落地/贴墙四段与源实现录屏对照 |
| **3** | 受击凹陷（GameplayCue）+ 死亡摊开 | 射击测试：凹坑在迎面一侧、会过冲回弹、死亡摊成一滩 |
| **4** | 拖拽 + 被咬：GAS TargetData 上行 + FastArray 复制 + 最多 3 个抓取者 | **Dedicated Server + 至少 2 个客户端**：A 拖 B 看，尖的位置与方向两端一致；Actor 转身 180° 尖不跑到背面；断线后 600 ms 自动归位 |
| **5** | DataAsset + AI（NavMesh + StateTree）+ LOD 三档 | 20 只同屏帧率；静止时 CPU 占用回落 |

阶段 0～3 是纯表现，没有网络风险。**真正要小心的是阶段 4。**

---

## 十一、自动化测试

`Plugins/SoftBodyCore/Source/SoftBodyCoreRuntime/Private/Tests/`：

1. **静止收敛**：从静止形状起步跑 2 秒，最大蒙皮误差 < `0.0015r`，且最终进入休眠。
2. **体积守恒**：施加拖拽后释放，`SurfaceVolumeError` 回到 0（±1e-3）。
3. **yaw 无关性**（坑 1 的守门人）：Actor yaw 取 0 / 90° / 180°，同一个世界命中点算出的突起向量三者相等。
4. **冲量不是状态**：`ApplyProjectileImpact` 一次后不再调用，1 秒内凹陷归零并休眠。
5. **净化边界**：`FSoftBodyGripState::Sanitize()` 对 NaN / Inf / 超界 / 缺字段全部整体作废。
6. **数值对数**（阶段 0 收尾）：用同一组输入分别跑源 TS 实现与 C++ 移植各 120 步，
   逐顶点误差 < 1e-4 r。做法：在 SkyLand 仓库写一个一次性脚本导出 CSV，放进本仓库当测试夹具。

---

## 十二、待定问题

1. **史莱姆在本工程里是什么身份**：海上冒险的野生生物？木筏上的宠物？决定它要不要浮力
   （源实现带 `buoyancy` 组件，本工程有 `NavalCore` 的浮力框架可复用）。
2. **是否需要玩家可拖拽**：阶段 4 是整份方案里唯一有网络风险、也最贵的一段。
   如果只要一只会抖、会被打、会死的生物，做到阶段 3 就够了。
3. **外壳要不要参与玩法碰撞**：源实现明确不参与（玩法只认胶囊）。若本工程想让史莱姆挡住玩家的形变部分，
   那是另一套需求，不在本文范围。
4. **美术方向**：源配色是无光照线稿风，本工程不是。第 6.6 节只锁结构不锁颜色。

---

## 交付说明

本仓库的执行环境没有 UE 工具链。按 `AGENTS.md`：实现阶段提交的 C++ **未经编译**，必须明确说明，
并列出编辑器内的执行步骤（编译哪些模块、重跑哪些 Python 脚本、如何在 PIE 验证）；
涉及复制的阶段 4，验证步骤必须覆盖 Dedicated Server + 至少两个客户端。
编辑器资产（DataAsset、AbilitySet、GameFeatureAction）一律由幂等的 Python 脚本生成，
遵循 `python-script-governance` 与 `lyra-editor-asset-automation` 两个 Skill 的门禁。
