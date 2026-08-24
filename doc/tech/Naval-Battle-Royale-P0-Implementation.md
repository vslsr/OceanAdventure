# 海战大逃杀 P0 实现说明

> 依据：`doc/designer/Naval-Battle-Royale-P0-P5-MVP-Loop-Plan.md` 第 2 章，以及总纲 7.7 / 7.8 / 7.10 / 7.11 / 8.x  
> 状态：C++ 与编辑器脚本已提交，**代码未经编译**（本仓库执行环境没有 UE 工具链）  
> 目标：把 P0「舰岛攻防玩具」的规则做成可运行系统，而不是把功能堆进一张清单

## 0. P0 的唯一主问题

> 角色轻武器侧移反制慢重炮、墙/炮窗改变射界、木筏用船身取向换取侧舷火力、主舵台受损后仍有抢修窗口 ——
> 这组关系本身是否有趣，且**不需要设计者口头解释弹道例外**。

所有实现取舍都服从这一句。凡是"玩家看不见、说不出、也影响不了"的复杂度，P0 一律不做。

## 1. 模块分层

严格遵守 `AGENTS.md` 的三层依赖，**同层之间没有任何依赖**：

| 层 | 位置 | 本次新增/改动 | 允许依赖 |
| --- | --- | --- | --- |
| 通用框架 | `Plugins/NavalCore/`（新增） | 船只状态机、承重模型、舵芯总成、弹道规则、重武器、可射击建造件 | Engine、BuildingCore、GameplayMessageRouter |
| 通用框架 | `Plugins/BuildingCore/` | `ABuildPlacedActor::ApplySourceDefinition` 改为 virtual | 不变 |
| 宿主/内容 | `Plugins/GameFeatures/Raft/` | 实现 `INavalBuoyancyControl`；海战建造件、T0 船体组件蓝图、救生筏 | 通用框架 |
| 玩法 | `Plugins/GameFeatures/OceanAdventure/` | GAS 能力、输入、HUD 查询、信标与单局规则 | 通用框架、LyraGame |

三条容易踩的边界，本次都按规范处理：

- **NavalCore 不认识 GAS、CommonUI、LyraGame。** 队伍归属通过 `IGenericTeamAgentInterface` 读取，作弊命令、存档恢复、编辑器脚本可以直接调用框架 API。
- **两个 GameFeature 互不引用。** 船体组件由 Raft 自己的 GameFeatureData 注入 `ARaftActor`；救生筏与地面炮的类路径写在项目 `Config/DefaultGame.ini` 里，由宿主工程把两边接起来。
- **`GameFeatureAction_AddComponents` 的反射桥下沉到 NavalCore**（`UNavalCoreAssetLibrary`），Raft 的编辑器脚本不再需要调用 OceanAdventure 的脚本。

## 2. P0 硬规则 → 代码

| 设计规则 | 落点 | 关键点 |
| --- | --- | --- |
| 墙阻挡敌我双方一切常规弹道 | `FNavalBallistics::ResolveShot` | 唯一弹道权威。Lyra 轻武器的射线本来就停在第一个阻挡体上，因此"墙挡子弹"天然成立 |
| 单向窗是唯一例外 | `UNavalFireWindowComponent::AllowsShot` | 按 **队伍 + 内外侧 + 出射角** 三重判定，**从不关闭碰撞**；窗被打穿后对所有人都是缺口 |
| 爆炸不穿墙 | `FNavalBallistics::IsExplosionOccluded` | 故意不复用 `ResolveShot`：总纲 7.7 第 6 条规定窗只放行弹丸，不放行爆炸 |
| 轻武器可破墙但效率低 | `UOceanAdventureGameplayAbility_LightWeapon::ApplyNavalHitEffects` | 对结构 28%、对船壳 18%（可在项目设置里调） |
| 轻武器从己方窗内向外射击 | 同类的 `AddAdditionalTraceIgnoreActors` | 开火前只把"这一发被授权穿过"的窗加入忽略列表，其余窗仍是实体 |
| 重炮：架设前摇 + 施工态 + 慢弹道 + 最小射距 + 离炮动作 | `ANavalHeavyWeaponActor` / `ANavalProjectile` / 站位能力基类 | 施工态 20–30% 耐久且**不能开火**；弹丸无重力无制导；离炮 0.45 秒锁 |
| 地面重武器自带支架，不要地基/战旗 | `ANavalHeavyWeaponActor::IsGroundSuitable` | 五点接地 + 坡度 + 台阶差；只判稳定与占位，不检查任何建筑关系 |
| 主舵台 = 交互点，舵芯座 = 受击体 | `ANavalHelmActor` | 舵轮 `NoCollision`，甲板下方较宽的加固座耐久 750（约普通墙 2.9 倍） |
| 舵芯失能只丧失操控，不等于沉船 | `UNavalHelmComponent` + `UNavalVesselComponent` | 两条独立生命值；核心归零→漂航，船壳归零→沉没倒计时 |
| 船壳归零先失能，保留抢修窗口 | `UNavalVesselComponent` | 20 秒倒计时、一次 7 秒抢修恢复 12% 船壳；继续挨打会**缩短**倒计时 |
| 失船后仍能参与 | `UOceanAdventureGameplayAbility_DeployLifeRaft` | 每队一次，仅在本队没有可用主船时；30% 船壳、70% 航速、**无建造目录**因此不能加墙加炮 |
| 吨位/浮力/推力三分档 | `UNavalLoadComponent` | 按总纲 7.11 的分档表；重要的是"重量永远算，容量只在部件正常时算" |
| 浮筒被打掉 → 危险吃水 | 同上 | 严重超载即进水，每秒 6% 船壳，给 10–15 秒抢救窗口 |
| 严重超载禁止继续加重 | `UNavalLoadComponent::CanAcceptPiece` | 但**永远允许**装浮筒或拆除，否则玩家会被自己卡死 |
| 一局必须结束 | `UOceanAdventureNavalMatchComponent` | 0:00 起算，5:30 信标启动，8:00 硬判定；淘汰 / 占领 / 超时三条出口 |
| 立即重开 | 同上 `RestartMatch()` + `NavalP0.RestartMatch` 控制台命令 | 直接 ServerTravel 回同一张图 |

## 3. 网络与权威

- 所有船只、部件、重武器状态由服务端写入并复制；客户端只做本地预览与表现。
- 玩家请求一律走 GAS 的 TargetData 通道（`CallServerSetReplicatedTargetData` + `AbilityTargetDataSetDelegate`），**没有任何自造的 Server/Client RPC**。操舵是一个 20Hz 的连续 TargetData 采样流，服务端逐条校验"你是不是我认为在掌舵的那个人"。
- 反馈（失败原因、警报、载重变化、命中）全部通过 `UGameplayMessageSubsystem` 广播，UI/音效/埋点各自订阅。
- 倒计时以**服务器时间戳**复制（`NavalTime::GetNetworkTimeSeconds`），而不是复制一个递减的秒数，中途加入或丢包的客户端读到的数字一致。
- 角色伤害是唯一一处跨层：NavalCore 广播 `FNavalProjectileImpactMessage`，玩法层的 `UOceanAdventureNavalDamageRelay` 把它变成 GameplayEffect。这样框架层完全不碰 GAS。

## 4. 编辑器操作步骤

1. **编译**：`NavalCoreRuntime`、`BuildingCoreRuntime`、`RaftRuntime`、`OceanAdventureRuntime`（顺序由 UBT 自行解析），然后重启编辑器，让新的 UCLASS 与原生 GameplayTag 注册进来。
2. **按顺序运行编辑器 Python**（都幂等，可反复运行修复资产）：
   ```
   import CreateRaftNavalAssets;  CreateRaftNavalAssets.main()      # Raft：海战建造件、船体组件、救生筏
   import CreateNavalP0Assets;    CreateNavalP0Assets.main()        # OceanAdventure：输入、能力集、体验
   import BuildNavalP0Map;        BuildNavalP0Map.build()           # 灰盒地图 L_NavalP0
   ```
   `CreateRaftNavalAssets` 依赖 `CreateRaftBuildPieceAssets` 已经建好基础模块与目录；
   `CreateNavalP0Assets` 依赖 `CreateOceanAdventureExperience` 已经建好 PawnData 与 GameFeatureData。
3. **再次重启编辑器**，让 GameFeature 带着新的 Action 重新注册。
4. **项目设置 → Game → Ocean Adventure Naval**：确认 `LifeRaftClass` / `GroundHeavyWeaponClass` 已指向脚本生成的蓝图，并把 `ProjectileDamageEffect` 指向一个带 `SetByCaller.Naval.Damage` 的 GameplayEffect（**未配置时重炮打不动角色，只会在日志里报警告**）。
5. **World Settings**：`L_NavalP0` 的 GameMode 用 Lyra 的，Experience 选 `B_Experience_NavalP0`。
6. **出生点必须是 `ALyraPlayerStart`**。体验现在会注入 `UOceanAdventureNavalSpawningComponent`，
   `ALyraGameMode::ChoosePlayerStart` 从此走 `ULyraPlayerSpawningManagerComponent`，
   而它只认 `ALyraPlayerStart`。`BuildNavalP0Map.py` 生成的就是这个类；
   手工摆的普通 `APlayerStart` 会被忽略，玩家会掉到原点。

## 5. 验证步骤

### 5.1 单机 PIE（最快回归）

| 检查 | 期望 |
| --- | --- |
| 走到舵轮按 `E` | 角色贴到舵台，WASD 变成油门/转向；再按 `E` 松手后船保持航向缓慢减速 |
| 在甲板放一面墙，站在墙后向外射击 | 子弹打在**自己的墙**上，不穿过 |
| 把墙换成单向窗，站在窗内向外射击 | 子弹放行；绕到窗外向内射击被挡；打烂窗后双向都能过 |
| 造浮筒/推进件，看 HUD 载重与推力条 | 跨档时分档变化，超载后转向明显变钝 |
| 打掉浮筒 | 进入危险吃水，船壳持续掉，补浮筒或拆重件可以救回来 |
| 打光船壳 | 进入 20 秒沉没倒计时，按 `R` 抢修一次可以拉回；第二次归零直接沉 |
| 沉没后按 `G` | 部署救生筏；再按一次没有第二个 |
| 按 `C` 在斜坡/悬崖边架炮 | 拒绝并给出单一原因；平地上可以架，施工态期间打不了炮 |
| 等到 5:30 | 信标启动；站进去 30 秒获胜；两队同时站着时进度冻结 |
| 控制台 `NavalP0.RestartMatch` | 立刻回到同一张图重开 |

### 5.2 Dedicated Server + 2 客户端（复制改动必测）

1. 用 `-server` 起专用服务器，两个客户端分别加入不同队伍。
2. **操舵权威**：A 掌舵时 B 也按 `E`，B 必须被拒绝（`Naval.Fail.SeatOccupied`），且 A 的船不受影响。
3. **状态一致**：A 打掉 B 的浮筒，两个客户端看到的载重分档、危险吃水提示、船壳条一致。
4. **倒计时一致**：船壳归零后两端读到的剩余秒数偏差应在一帧内。
5. **夺船**：B 登上 A 的空船，在舵台持续交互，4 秒后开始改旗、11 秒完成；A 回来打断后进度缓慢回退而不是清零；改旗完成后**窗、炮、储物一起换队**。
6. **反作弊回归**：客户端断点/修改本地瞄点后开炮，服务端仍按自己的射界与最小射距拒绝。
7. **救生筏**：A 队沉船后部署救生筏，两端都看到它；A 队第二次尝试被拒绝。
8. **掉线释放占用**：A 占住重炮后**强杀客户端进程**（不是正常退出）。几秒内 B 必须能占上同一门炮，炮口停在 A 最后的角度。掌舵同理：A 掌舵时掉线，B 必须能接管方向盘。
9. **重连锚点**：A 重连后应在**炮旁边**出生，而不是默认出生点；此时炮的 `WeaponOperator` 已经是 B，A 按 `E` 应被拒（`Naval.Fail.SeatOccupied`）。
10. **锚点一次性**：A 重连后正常死亡一次，必须在**默认出生点**重生，不能再被拉回掉线位置。
11. **船在动时掉线**：让 A 在航行中的船上掉线，等船开出几百米后重连，A 应出现在**船现在的位置**上，不是掉线时的绝对坐标。

### 5.3 对照 P0 通过门槛

盲测记录表直接对应设计文档 2.6 的八项指标。其中**"规则一致性：墙后受直击/爆炸/压制伤害的错误为 0"** 是唯一不能用"体验不错"糊过去的一项 —— 一旦出现，先查是不是有伤害路径绕过了 `FNavalBallistics`。

## 6. 明确没有做的部分

- **美术与音效**：全部是引擎基础几何体。P0 只要求团队归属、窗内外、重炮预警、受损状态可读。
- **HUD 控件本体**：C++ 提供了数据源（`UOceanAdventureNavalStatics::GetVesselHudState`）与全部消息频道，UMG 控件需要在编辑器里搭。
- **受击打断抢修/建造**：目前抢修被"离开距离"和服务端复检拦住，还没有接 Lyra 的 `AbilityTagRelationshipMapping` 做"受伤即取消"。这是 P0 盲测前建议补的一条。
- **施工件的半透明骨架表现**：施工态在数据上完全成立（低耐久、能挡弹、不能开火），但还没有专门的材质表现。
- **P0 之外的一切**：经济、PvE、缩圈、Seed 地图、T2/T3、局外成长，按设计文档要求一律不做。

## 7. 已知取舍

- **Raft 一被启用就带船体组件**。船体状态由 Raft 自己的 GameFeatureData 注入，因此现有的建造沙盒体验里的木筏也会带上它们。选它是因为另一条路（由玩法 Feature 注入）会让 OceanAdventure 引用 `ARaftActor`，直接违反"GameFeature 之间不得产生依赖"。组件本身是数据驱动的，沙盒里不碰就不产生行为。
- **操舵时角色被 attach 到舵台并进入 `MOVE_None`**。这是 P0 里最直接、跟随移动平台最稳的做法；如果后续要做舵台上的位移动画，这一层需要换成一套 root-motion 或座位系统。
- **重炮弹丸的命中只在服务端结算**，客户端只跑同一条直线做表现。P0 尺度下足够；如果之后弹速调低到需要更精细的表现同步，再考虑补一条弹道快照。

## 8. 掉线与重连

掉线把"能力"和"世界状态"分成了两半：**能力那半会随连接一起消失，世界状态那半不会**。两个子问题的真值住在不同的地方，因此分开解。

### 8.1 释放占用 —— 炮/舵自己的不变量，放在 NavalCore

`ReleaseOperator` / `ReleaseHelm` 原本只有一个调用点：站位能力的 `EndAbility`。掉线走不通这条路 ——
连接断开 → PlayerController 销毁 → UnPossess → PlayerState 销毁 → 挂在 PlayerState 上的 ASC 一起没了 →
Ability 实例连同 `EndAbility` 一起消失，释放请求永远发不出去。

只靠 GC 把 `UPROPERTY(Replicated) TObjectPtr<AActor> Operator` 置空是不够的：它不会调 `ForceNetUpdate()`，
也不会广播武器/舵台状态消息（UI、音效、任何订阅方都收不到"位置空出来了"），时机取决于 GC，
而且只要有一条路径让 Pawn 在无控制器的情况下留下来，那门炮/那个舵就再也没人能用。

判据："operator 必须是一个活着的、有控制器的 Actor"是炮和舵自己的不变量，不经过 GAS，
作弊命令、存档恢复、编辑器工具也要能直接调 —— 按 `AGENTS.md` 的三层规范，**属于 NavalCore**。

两道保险，都汇入**同一条**释放函数，所以 `ForceNetUpdate` + 状态广播自动带上，
UI 收到的和玩家正常按 `E` 离开时一模一样：

1. **占用时绑 `AActor::OnDestroyed`**（`ANavalHeavyWeaponActor::BindOperatorDestroyed` /
   `UNavalHelmComponent::BindOperatorDestroyed`）。Pawn 一销毁就走正常释放。
2. **服务端低频兜底**（1 秒一次的 timer，不占 Tick）：operator 还在但已经没有控制器，
   且持续超过 `OrphanGraceSeconds`（默认 1.5 秒）就释放。宽限期是为了避开 possess 交接、
   以及 Lyra 死亡流程里 `DetachFromControllerPendingDestroy()` 到 Pawn 真正销毁之间的无控制器窗口。

`IsStationStillValid` 只在客户端的控制采样里跑，掉线的客户端根本不会再跑它，所以服务端必须有自己的检查。
**没有**在 `GameMode::Logout` 里遍历全世界找占用 —— `AGENTS.md` 禁止 `TObjectIterator` /
`GetAllActorsOfClass` 全局遍历，而且 Logout 时关联反而不好拿。

### 8.2 重连锚点 —— 玩法层的重生策略，放在 OceanAdventure

`ALyraGameMode` 继承自 `AGameModeBase`。引擎的 `InactivePlayerArray` / `AddInactivePlayer` /
`FindInactivePlayer` / `OverridePlayerState` 那一整套重连状态恢复是 **`AGameMode`（非 Base）**独有的，
在 Lyra 里**根本不存在**。重连回来的是全新的 PlayerController + PlayerState + Pawn + ASC，
旧 PlayerState 上的一切都没了。想跨掉线保留什么，就得存在比 PlayerState 活得久的地方。

**只存重生锚点，不存占用状态。** 这是整个设计里最关键的一个决定：不想恢复的状态，就根本不要记录它。
掉线那一刻角色本来就 attach 在炮台上，他的位置**就是**操作位的位置，
所以存一个位置，"在炮附近出生"自动成立，而且**结构上不可能意外恢复占用**。
反过来若存"他在操作炮台 X"，就要为"炮被打爆 / 船沉了 / 队友接手 / 队伍变了"各写一条 fallback。

| 决定 | 落点 | 理由 |
| --- | --- | --- |
| 键用 `FUniqueNetIdRepl` | `FOceanAdventureNavalReconnectAnchor::PlayerId` | Actor 指针已销毁；`APlayerState::PlayerId` 每次连接重新分配；玩家名可改可重名 |
| 非 Shipping 下按玩家名降级匹配 | `FindAnchorIndex` | PIE 本地多人的 UniqueNetId 可能为空，否则整个功能在编辑器里无法测试 |
| 有船时存**船空间相对坐标** | `bVesselRelative` + `Location` | 船在动。掉线三十秒船已经开出几百米，绝对坐标会把人扔进海里 |
| 船已销毁 → 放弃锚点 | `ResolveWorldLocation` 返回 false | `TWeakObjectPtr` 为空就是唯一需要处理的失效情形 |
| 存在 GameState 组件上 | `UOceanAdventureNavalReconnectComponent` | GameState 活满整局。PlayerState 跟着掉线消失，Pawn 同理，炮会被打爆也会换手 |
| 在 `Logout` 记录 | `FGameModeEvents::OnGameModeLogoutEvent()` | 唯一 PlayerController / PlayerState / Pawn 都还在的时机；用全局委托而不是 GameMode 子类，才符合 GameFeature 的注入模型 |

顺序上有一个天然性质：**记锚点需要 Pawn 还在，清占用需要 Pawn 已经没了**，
两件事对 Pawn 生命周期的要求正好相反，而 `Logout → Pawn 销毁` 的先后同时满足了两者，不需要额外协调。

### 8.3 用锚点 —— `OnFinishRestartPlayer`，不是 `OnChoosePlayerStart`

`ULyraPlayerSpawningManagerComponent` 留了两个虚函数，由 `ALyraGameMode` 代理调用。
`UOceanAdventureNavalSpawningComponent` **只覆盖 `OnFinishRestartPlayer`**。

不用 `OnChoosePlayerStart` 直接返回炮：`AGameModeBase::SpawnDefaultPawnFor` 会拿返回 Actor 的 transform
直接生成 Pawn，那是炮的原点 —— 人会卡在炮膛里；落点被挡住时 `SpawnActor` 的碰撞处理还会把人顶到
不可预期的地方甚至生成失败。`OnFinishRestartPlayer` 的性质好得多：
**先在正常出生点安全生成，再尝试挪过去；挪不动就保持原样**，最坏情况只是出生点不理想。

`UNavalSpawnStatics::FindClearSpotNear`（NavalCore，因为作弊传送和存档恢复也要用同一套判断）
先把候选点**向下投到脚下的甲板**再做容积测试 —— 重炮的 `OperatorPoint` 相对高度是 `(-110, 0, 90)`，
不下投的话玩家会在空中出生然后掉下来；找不到空位就一圈圈向外试，全失败就返回 false，保留默认出生点。

三个必须处理的边界：

1. **锚点必须消费。** `OnFinishRestartPlayer` **每次重生都会调**，不只是重连时调。
   所以 `ConsumeAnchor` 用 `ON_SCOPE_EXIT` 挂在函数出口，任何一条返回路径都会消费。
   忘了消费，玩家每次死亡重生都会被传回掉线位置 —— 这是这套设计里最容易踩的坑。
2. **过期时间。** 一局 8 分钟（`HardTimeLimitSeconds = 480`），锚点默认存 120 秒，超时按新玩家处理。
3. **`RestartMatch()` 清表。** ServerTravel 会连 GameState 一起换掉，表天然是空的；
   显式清一次是为了不让正确性依赖于"重开恰好是这么实现的"。

### 8.4 没有做：重连后自动回到炮上

不要在服务端直接恢复 `WeaponOperator`。掉线期间队友可能已经接手，强行恢复会把队友踢下来；
占用需要走完整的 `CanOperate` 校验（队伍、部件状态、距离），服务端单方面写就是绕过全部安全检查；
而且能力激活带预测 key，服务端凭空"恢复"一个能力状态，客户端没有对应的 ability 实例，表现层和真值会直接脱节。
正确做法是重连后照常在炮边出生，由客户端**发起一次正常的交互请求**，所有校验照跑，失败就是失败。

## 9. 站位提示（NavalProximityComponent）

走到炮/舵旁边之前没有任何反馈，玩家只能靠按 `E` 试错：要么进站，要么收一个 `Fail.TooFar`。
这一节补的就是这半边。

### 9.1 为什么不照搬 Lyra 的交互系统

Lyra 的 `UAbilityTask_GrantNearbyInteraction` 是"靠近→授予能力"：扫描附近实现了
`IInteractableTarget` 的 Actor，把它们的 `InteractionAbilityToGrant` 用 `GiveAbility` 发给玩家。
那套解决的是**开放集合**——拾取物、门、按钮，种类事先不可知，交互逻辑天然属于物件。

海战站位是**封闭集合**：舵 + 重武器，两种，在 PawnData 编写期就完全确定，
出生时已经由 `DA_AbilitySet_OceanNaval` 全部授予。再动态授予一次买不到任何东西，
只会多出 `GiveAbility` 抖动；而且 Lyra 那个任务**只授不撤**，走远了也收不回来。

另有三条硬阻碍：占用是多秒的长时状态（挂载、20Hz 控制采样、退出锁），
不是 `TriggerAbilityFromGameplayEvent` 那种一次性事件；`ANavalHeavyWeaponActor` 没有 ASC，
走 `InteractionOption` 路线(2) 得给它挂一个，而占用归属正是刻意留在世界状态、不走 GAS 的东西；
瞄准采样要沿 GAS TargetData 做客户端预测→服务端复验，交互系统没有这条通道。

所以：**授予留在 PawnData，扫描只用来出提示。**
这与 `UOceanAdventureBuildProximityComponent` 的判断一致（见该头文件注释）。

### 9.2 NavalCore：站位注册表 + `INavalStationInterface`

`UNavalRegistrySubsystem` 新增站位列表，炮与舵台在 `BeginPlay` 自行登记、`EndPlay` 注销。
提示每 0.25s 每玩家问一次"附近有没有能用的东西"，用 sphere overlap 是纯浪费——
站位集合很小、已知、而且自己会报到。

`INavalStationInterface` 统一三个问题，消掉调用方按具体类型分支的老毛病：

| | `ANavalHeavyWeaponActor` | `ANavalHelmActor` |
|---|---|---|
| `GetStationWorldLocation` | `GetActorLocation()` | 转发 `UNavalHelmComponent::GetHelmWorldLocation()` |
| `GetStationInteractionRange` | `InteractionRange` | 转发组件的 `InteractionRange` |
| `CanOperateStation` | `CanOperate()` | 转发 `CanOccupy()` |

两个查询：`FindNearestStation`（显式半径，能力用的宽松预筛）、
`FindReachableStation`（走站位自己的受理检查，提示用）。
`UOceanAdventureNavalStatics::FindNearestStationActor` 也改走注册表，
不再做 overlap——权威侧 `CanOperate` 本来就是原点距离判定，两边从此同一套度量。

### 9.3 OceanAdventure：`UOceanAdventureNavalProximityComponent`

挂在 `AOceanAdventurePawn` 上，**只在本地控制端运行**（提示是给看屏幕的人的，
服务端每次请求都会重跑站位的受理检查，不需要这个 tag）。维护两样东西：

- `Status.Naval.StationAvailable` 松散 tag
- `Naval.Message.Station.Prompt` 消息（带 `StationActor`，HUD 直接绑）

两点值得注意：

1. **迟滞方向是反的**。Build 那个组件放宽*离开*半径，因为它的 tag 门控着能力，
   迟滞是为了别在边界打断能力。这个 tag 只驱动提示，而在服务端会拒绝的距离上显示"按 E"
   就是骗人——所以余量往内收：`EnterRangeScale = 0.9` 进、`1.0` 出，
   提示在站位开始拒绝之前就已经消失。
2. **提示走的是站位自己的 `CanOperateStation`**，不是距离近似。
   敌方的炮、别人已经坐着的炮、还在建造中的炮都不会亮。

已经在站上时组件让路（检测 `Status.Naval.OperatingHeavyWeapon` / `Status.Naval.Steering`），
那时该显示的是"按 E 离开"，归站位能力管。

### 9.4 编辑器步骤增量

`CreateNavalP0Assets.py` 新增 `OceanNaval_AddProximityComponent`（GameFeatureData 上的
AddComponents，client+server 都为 true——listen server 的主机也是本地玩家，
实际过滤由组件的 `IsLocallyControlled()` 早退完成）。按第 4 节原流程重跑该脚本即可。

HUD 侧尚未接线：tag 和消息都已经在了，**浮动提示 widget 还没做**。
