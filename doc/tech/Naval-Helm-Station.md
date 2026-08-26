# 船舵（主舵台）：可建造、可搬运、按 E 上舵

## 这一版做了什么

- 玩家初始木筏的甲板上有一座**船舵 Actor**（`ANavalHelmActor`），走近按 `E` 吸附上去。
- 船舵也是**建造件**（`DA_BuildPiece_Raft_Helm`）和**可搬运物**（`UCarryableComponent`）：
  按 `B` 造一座、按 `F` 抬走放到别处，和甲板炮完全同一套规则。
- 上舵后角色被钉在舵位上（`OperatorPoint`），`W/S` 变成进退、`A/D` 变成转向，控制的是船体而不是脚。
- 再按一次 `E` 下舵：输入还给走路，船保持航向并自然减速（不急停、不自动驾驶）。
- 舵轮会跟着当前实际转向量转动，作为纯表现反馈。

整条链路与"炮"完全同构，只是把"开火"换成了"操舵"：

| | 重武器 | 船舵 |
| --- | --- | --- |
| 世界里的 Actor | `ANavalHeavyWeaponActor` | `ANavalHelmActor` |
| 谁在用它 | Actor 自己的 `WeaponOperator` | 船上的 `UNavalHelmComponent::Operator` |
| 上/下站位的能力 | `UOceanAdventureGameplayAbility_OperateHeavyWeapon` | `UOceanAdventureGameplayAbility_OperateHelm` |
| 共同基类 | `UOceanAdventureGameplayAbility_NavalStation` | 同左 |
| 输入 | `InputTag.Naval.Interact`（E） | 同左 |

## 按下 E 之后发生了什么

```
IA_Naval_Interact (E)
  └─ DA_InputConfig_OceanNaval → InputTag.Naval.Interact
       └─ DA_AbilitySet_OceanNaval 授予的 OperateHelm / OperateHeavyWeapon 同时尝试激活
            ├─ OperateHelm: FindStationInRange() 用球形 Overlap 找最近的 ANavalHelmActor
            │    ├─ 找不到 → 立刻结束（这次按键就当作没有舵）
            │    └─ 找到 → 客户端先本地吸附，同时把 Occupy 请求走 GAS TargetData 发给服务端
            │         └─ 服务端 UNavalHelmComponent::TryOccupy() 重新校验：
            │              队伍、残骸状态、座位是否被占、与舵的距离 ≤ InteractionRange
            │              拒绝 → EndAbility，客户端的本地吸附随之回滚
            └─ OperateHeavyWeapon: 被 Status.Naval.Steering 挡住（ActivationBlockedTags）
```

上舵成功后：

- `EnterStationPresentation()` 把角色 `AttachToActor` 到船舵上（不是每帧 teleport），
  所以角色天然跟着甲板漂；`MOVE_None` + `Gameplay.MovementStopped` 停掉走路。
- `UOceanAdventureHelmInputComponent::EnableHelmInput()` 压入优先级 2 的 `IMC_OceanHelm`，
  `W/A/S/D` 被 `IA_Ocean_Helm_Throttle` / `IA_Ocean_Helm_Steer` 吃掉，顶视角移动看不到这几个键。
- 之后每 `ControlSampleInterval`（0.05s）采样一次，仍旧走 TargetData 通道发给服务端；
  服务端 `SetControlIntent()` 只接受"它认为正在掌舵的那个 Actor"发来的值，其余一律丢弃。
- 再按一次 `E`：`UAbilityTask_WaitInputPress` 收到同一个输入 → `EndAbility` → 发 Release、
  解除附着、弹出 `IMC_OceanHelm`、并上 0.45s 的 `Status.Naval.StationExitLock`。

## 船舵 Actor 的构成

`ANavalHelmActor`（`Plugins/NavalCore/.../Naval/NavalHelmActor.h`）：

| 组件 | 作用 |
| --- | --- |
| `ConsoleMesh` | 舵台底座，**无碰撞**——设计 8.3.1 明确不允许远处一枪打瘫全船 |
| `WheelPivot` / `WheelMesh` | 舵轮，跟随 `GetSteerIntent()` 插值转动，纯表现 |
| `OperatorPoint` | 角色站位（舵台后方、面向船首），`GetOperatorTransform()` 提供给能力 |
| `CoreSeatCollision` | 加固舵芯座：唯一的受击体，也是 Overlap 找站位时命中的体积（`WorldDynamic`）。被抬起时碰撞整体关闭，因此搜不到，也打不中 |
| `CorePart` | `UNavalPartComponent`，750 耐久 ≈ 普通墙的 2.5–3 倍 |

它不持有"谁在掌舵"这件事——归属、夺船、控制量全在船上的 `UNavalHelmComponent`，
Actor 只是玩家能走到、能看到、能打到的那一层。

两个 Mesh 默认用引擎基础体素灰盒，所以**没有美术资源也能直接看到并交互**。

## 一条船可以有几座舵台

**一座舵芯，多个入口。** 归属、控制量、夺船进度只存在于船上那一个
`UNavalHelmComponent`；甲板上的每座 `ANavalHelmActor` 都只是走上去开船的入口。

- **出厂那座**：`UNavalHelmComponent::BeginPlay()` 在服务端按 `HelmLocalOffset` /
  `HelmLocalYaw` 生成并 attach，`EndPlay` 时销毁。想要"必须先造舵才能开船"的船型，
  把 `HelmActorClass` 留空即可。
- **建造出来的**：`DA_BuildPiece_Raft_Helm` 的 `SpawnActor` fragment 生成 `BP_Raft_Helm`，
  `UBuildStructureVisualComponent` 负责 attach 到甲板。
- **搬来的**：抬起时附着到角色身上、关碰撞；放下时附着到落点所在的物件上。

三条路径共用同一个判断：**这座舵台当前 attach 在谁身上，它就属于那条船**
（`ANavalHelmActor::GetHelmComponent()` 每次现算，不缓存）。所以抬起一座舵台就等于
把这个入口从船上摘走，不需要任何额外通知；放到另一条船的甲板上，它立刻就是那条船的舵台。

**够不够得着由舵台自己判定**，不再由船判定：`ANavalHelmActor::CanOperate()` 检查与
**这一座**的距离和它自身的损坏状态，`UNavalHelmComponent::CanOccupy()` 只回答全船性的问题
（队伍、残骸、座位是否被占）。一条船上有前后两座舵台时，这是唯一说得通的分法。

木筏的这套船体组件由 Raft 这个 GameFeature 自己注入（`RaftNaval_AddVesselComponents`）：

```
Raft.uasset (GameFeatureData)
└─ ARaftActor ← BPC_NavalVessel_RaftT0 / BPC_NavalLoad_RaftT0
                BPC_NavalHelm_RaftT0 / BPC_NavalMovement_RaftT0
                        └─ HelmActorClass = BP_Raft_Helm  (/Raft/Naval/BP_Raft_Helm)
                           HelmLocalOffset = (-120, 0, 25)
```

`BP_Raft_Helm` 由 `Plugins/GameFeatures/Raft/Content/Python/CreateRaftNavalAssets.py` 生成，
舵轮模型取自 `/NavalCore` 下的 `SM_Naval_HelmWheel`（按资产名解析，美术目录怎么分组都行），
挂在 `WheelMesh` 上——它在 `WheelPivot` 下面，会跟着转向量转。舵台底座仍是灰盒，
补一个 `SM_Naval_HelmConsole` 挂到 `ConsoleMesh` 即可。微调站位也改这个蓝图，
不要回头改 NavalCore 的 C++ 默认值。

## 已知限制

**抬走一件建造出来的东西，格子不会被释放。** 建造记录（`FBuildPieceEntry`）与 Actor 是两回事：
`UCarryableComponent` 只搬 Actor，`UBuildStructureComponent` 那边的占用、连通性和吨位仍然算着。
所以把建造出来的舵台抬走后，那个格子还是占着的，吨位也还在。甲板炮同样如此——这是搬运系统
V1 就存在的缺口，不是舵台引入的。要修的话是在抬起时调 `TryRemovePiece()` 把格子退掉，
放下时重新走一次放置校验；那会牵动建造的资源返还与连通性规则，单独做。

## 常见排查

- **按 E 没反应**：先看 `LogOceanAdventure` 里的 `[NavalStation] No station found`。
  多半是站得太远（`StationSearchRadius` 300cm，服务端另有 `InteractionRange` 260cm），
  或者这条船根本没被注入 `UNavalHelmComponent`（Raft Feature 未激活）。
- **上去了立刻被弹下来**：服务端拒绝，日志里会打出 `Server occupy refused`；
  对照 `CanOccupy()` 的四项：队伍、残骸、座位被占、距离。
- **上了舵船不动**：`AcceptsControlInput()` 为假（舵芯被打坏 / 正被夺船 / 船已成残骸），
  或推进力为 0——舵本身不产生动力，推进来自推进器部件。
- **搬到别处的舵台按 E 没反应**：它没 attach 在船上（放在了地形上），
  `GetHelmComponent()` 返回空 → `Fail_NotOperational`。这是有意的：舵台离船就不能开船。
