# 插件职责总览

本工程把玩法拆成三层，依赖只能自上而下（详见 `AGENTS.md`）：

```
玩法层        Plugins/GameFeatures/OceanAdventure     拥有玩家 Pawn，GAS 能力、输入、UI
宿主/内容层   Plugins/GameFeatures/Raft | TopDownFeature | SimpleExperience
通用框架层    Plugins/NavalCore | OceanCore | BuildingCore | CarryCore
```

通用框架层只依赖 Engine 与其它通用插件，**不得**依赖 `LyraGame`、`GameplayAbilities`、
`CommonUI` 或任何 GameFeature。判据很实在：作弊命令、存档恢复、编辑器工具都不经过 GAS，
它们必须能直接调框架 API。GameFeature 之间则完全不许互相依赖。

| 插件 | 层 | 一句话职责 |
| --- | --- | --- |
| `OceanCore` | 通用框架 | 海面与岛屿地块的程序化生成与流送，以及全局唯一的水面采样真值 |
| `BuildingCore` | 通用框架 | 与宿主无关的网格建造：格子、连通性、预览、复制 |
| `NavalCore` | 通用框架 | 船只状态、承重浮力推力、舵权、"墙挡一切"的弹道规则、重武器 |
| `CarryCore` | 通用框架 | 谁正扛着什么：世界实体的抬起 / 放下，不涉及背包 |
| `Raft` | 宿主/内容 | 把上面几套框架落在一艘会浮的木筏上：可建造、可航行、可被打沉的移动平台 |

---

## OceanCore

`Plugins/OceanCore/` · 模块 `OceanCoreRuntime` + `OceanCoreEditor` · 依赖 `ModularGameplay`

**海从哪来，水面高度谁说了算。**

- `UOceanWorldManagerComponent` — 挂在 GameState 上，服务端权威地持有当前世界的活动地块集合。
- `UOceanChunkInvokerComponent` — 给 Pawn / 船装上它，就成为"需要地块"的来源；地块围绕 invoker 流送。
- `AOceanChunkActor` — 一个地块的复制容器。服务端只初始化一次，客户端**确定性地**从复制状态
  重建表现，因此地形和海面不靠传网格数据同步。
- `FOceanWaterSurfaceSample`（位置 / 法线 / 流速）— 全工程唯一的水面采样接口。木筏浮力、
  未来的漂流物和钓鱼都从这里取值，谁都不许自己算一遍波形。
- `UOceanGenerationSettings` — 生成参数；`OceanCoreEditor` 提供编辑器侧工具。

它不认识玩家、不认识 GAS，也不认识木筏。

## BuildingCore

`Plugins/BuildingCore/` · 模块 `BuildingCoreRuntime` · 无插件依赖

**在"任何宿主"上按格子搭东西。**

- `IBuildStructureHost` — 关键抽象。宿主（木筏甲板、岛上地基）实现它来回答：结构空间在哪、
  挂到哪个组件、格子设置是什么、哪些格是锚点、要不要求连通。框架因此完全不知道自己搭在船上还是岛上。
- `UBuildStructureComponent` — 服务端权威的结构真值（哪一格放了什么、连通性、拆除规则）。
- `UBuildPieceDefinition` + Fragment — 每种构件的数据资产。有状态的构件（火炮、箱子、篝火）
  用 `Fragment_SpawnActor` 生成真 Actor，纯装饰件保持实例化网格，代价接近零。
- `UBuildPieceCatalog` — **只能追加**的网络索引表。复制时传索引而不是资产指针，中间项不得插入或删除。
- `UBuildPreviewComponent` / `UBuildStructureVisualComponent` — 幽灵预览与实例化渲染。
- `UBuildStructureSubsystem` — 世界内所有结构的注册表，替代"每帧遍历全世界找最近的结构"。
- `UBuildResourceSource` / `BuildCheats` — 资源扣减接口与创作模式命令（不经过 GAS 的那条路）。

## NavalCore

`Plugins/NavalCore/` · 模块 `NavalCoreRuntime` · 依赖 `BuildingCore`、`ModularGameplay`、`GameFeatures`

**一艘船在玩法上意味着什么。**

- `UNavalVesselComponent` — 船的服务端真值：归属、船体耐久、当前处于哪个"零点"。
  设计上船体和舵芯是两条命：舵芯归零失去主动控制，船体归零开始 15–25 秒沉没倒计时并留一次抢修。
  沉船是一种**状态**，不是 despawn。
- `UNavalPartComponent` — "船上一切能被打的东西"共用的耐久 + 施工窗口：架设期无碰撞 →
  施工期能挡弹但不提供功能 → 可用。浮筒、墙、舵、炮读同一套规则。
- `UNavalLoadComponent` — 承重 / 浮力容量 / 推力（设计 7.11）。实例化构件的重量直接读定义，
  会生成 Actor 的部件通过 part 组件上报 —— 打掉浮筒会停掉它的浮力却不减它的重量，这正是要的不对称。
- `UNavalMovementComponent` — 只写平面位移和 Yaw；Z、Pitch、Roll 归宿主的浮力。两者不打架。
- `UNavalHelmComponent` — 舵芯总成：甲板上的舵轮（进入控制、被夺船的起点）、
  舱内的加固芯座（真正的受伤体）、船尾舵叶（独立部件），一次命中打不掉全部。
- `FNavalShotQuery` / `NavalBallistics` — 全工程共用的"墙挡一切"判定，预览和服务端裁决同一份代码。
- `UNavalFireWindowComponent` — 单向射击窗，是墙规则唯一的例外，靠队伍 + 深度 + 出射角判定，
  绝不靠关碰撞（否则敌方火力会顺着开口打回来）。
- `ANavalHeavyWeaponActor` — 重武器本体：架设、操作位、射界、最小射距、慢弹道、可被击毁。
  地面架设和甲板安装是同一个类，差别只在支援条件。
- `ANavalProjectile`、`ANavalBuildPieceActor`、`UNavalRegistrySubsystem`（船只注册表）、
  `INavalBuoyancyControl`（向宿主要浮力开关的接口）。

## CarryCore

`Plugins/CarryCore/` · 模块 `CarryCoreRuntime` · 无插件依赖

**谁正扛着什么。**

- `UCarryableComponent` — 挂在可被搬运的 Actor 上。唯一复制真值是"谁在搬"，
  外加放下时的落点与朝向；服务端与客户端跑同一个 `ApplyCarryState()` 完成挂载、卸载、碰撞开关。
- `UCarrierComponent` — 一副手，**同时只能持有一个**。按键时一次 overlap 找目标，
  服务端算落点并把站在占地里的角色轻推开；角色被销毁时把物件放回地面。

刻意不做成背包：设计要求搬运本身就是代价（双手被占、减速、可被抢），
放进物品栏会同时删掉这三样。背包留给资源和消耗品。细节见 `doc/tech/Carry-System-V1.md`。

## Raft

`Plugins/GameFeatures/Raft/` · 模块 `RaftRuntime` · 依赖 `BuildingCore`、`NavalCore`、`OceanCore`

**把上面三套框架落地成一艘具体的船。**

- `ARaftActor` — 复制的移动平台，同时实现 `IBuildStructureHost`：木筏甲板就是建造宿主。
  角色把 `DeckCollision` 当作 CharacterMovement 的 movement base 站在上面。
- `URaftBuoyancyComponent` — 仅服务端的运动学浮力，四个浮筒点向 OceanCore 采样水面；
  实现 `INavalBuoyancyControl`，船变成残骸后就不再被托在水线上。结果通过标准
  `AActor::ReplicatedMovement` 发给客户端。
- `URaftBuildPieceDefinition` / `RaftDefinition` — 木筏专属的构件与整船定义。
- `Content/Python/` 下的一组幂等脚本负责生成这些资产（`BuildRaftFeature.py` 跑完整流程，
  `ValidateRaftFeature.py` 只读校验）。

Raft 依赖 OceanCore，**从不**依赖 OceanAdventure；反过来，重武器等玩法能力也不放在这里。

---

## 一句话记法

- **OceanCore** 给世界；**BuildingCore** 给格子；**NavalCore** 给船的规则；**CarryCore** 给手。
- **Raft** 是它们四个第一个共同的宿主；**OceanAdventure** 是唯一拥有玩家和 GAS 的那一层。

---

## 依赖关系

只有五条边，全部自上而下，无环：

```
                    OceanAdventure (玩法层 · 唯一有玩家 Pawn 和 GAS 的一层)
                    │  依赖以下全部 + LyraGame + GameplayAbilities + CommonUI
        ┌───────────┼───────────┬──────────────┐
        │           │           │              │
      Raft ────────►│           │              │
        │  │        │           │              │
        │  ├──────► NavalCore ──┤              │
        │  │            │       │              │
        │  └──────────► BuildingCore           │
        └────────────► OceanCore          CarryCore
                                          (谁都不依赖，也只被玩法层用)
```

| 边 | 类型 | 为什么存在（具体耦合点） |
| --- | --- | --- |
| `NavalCore → BuildingCore` | Public | 船上能被打的东西**就是**建造构件。`ANavalBuildPieceActor` 继承 `ABuildPlacedActor`；`UNavalPieceFragments` 是挂在 `UBuildPieceDefinition` 上的 Fragment；`UNavalLoadComponent` 通过 `UBuildStructureComponent` + `UBuildPieceCatalog` 读出实例化构件（甲板、墙）的重量 |
| `Raft → BuildingCore` | Public | `ARaftActor` 实现 `IBuildStructureHost`，甲板即建造宿主 |
| `Raft → NavalCore` | Public | `URaftBuoyancyComponent` 实现 `INavalBuoyancyControl`，让船变成残骸后不再被托在水线上 |
| `Raft → OceanCore` | **Private** | 浮力向 `UOceanWorldManagerComponent` 要 `FOceanWaterSurfaceSample`。私有依赖 = 不出现在 Raft 的公开头文件里，外部无法顺着 Raft 摸到 OceanCore |
| `OceanAdventure → 全部四个` | Public | 玩法层是唯一允许同时认识它们的地方：GAS 能力在这里把"框架 A 的对象"和"框架 B 的规则"接起来 |

### 没有的边，比有的边更重要

- **OceanCore ↔ BuildingCore ↔ CarryCore 三者互不相识。** 它们是三块独立地基，可以单独抽走。
- **OceanCore 不依赖 NavalCore。** 水面不知道船存在；反过来 NavalCore 也不直接依赖 OceanCore ——
  船怎么浮是宿主的事，框架只通过 `INavalBuoyancyControl` 要一个开关。
- **CarryCore 不依赖 NavalCore。** "被扛起的是一门炮"这件事只有玩法层知道：
  `UOceanAdventureGameplayAbility_Carry` 里那些判断（甲板炮不能扛、有人操作不能扛、未建成不能扛）
  是**唯一**同时 include 两边头文件的地方。这是刻意的 —— 换成扛核心箱、扛货物时框架一行不用改。
- **GameFeature 之间零依赖。** `Raft` 不认识 `OceanAdventure`，`OceanAdventure` 也不认识 `Raft` 的类；
  需要共享就下沉成通用插件，需要跨越就用接口（`IBuildStructureHost`、`INavalBuoyancyControl`）。

### 两种跨层手段

框架之间从不直接调用，只有两种连接方式，都在通用插件里定义、由宿主实现：

1. **宿主接口** —— `IBuildStructureHost`（"我能被搭建"）、`INavalBuoyancyControl`（"我能浮"）。
   框架定义提问，宿主负责回答。
2. **组件注入** —— GameFeature 的 `AddComponents` Action 把能力挂到别人的 Actor 上，
   Actor 本身只需注册成 GameFramework 组件接收者。`UCarryableComponent` 挂到
   `ANavalHeavyWeaponActor` 走的就是这条路：两个插件的 `.Build.cs` 都不必提到对方。

### 新增依赖前先自问

- 这条边是不是从下往上（框架依赖玩法 / GameFeature 依赖 GameFeature）？是就一定不行。
- 能不能用接口代替直接 include？能就用接口。
- 只有玩法层需要知道两边？那就把胶水写进 GAS 能力里，别写进框架。
