# 船舵（主舵台）：普通木筏按 E 上舵，LifeRaft 站上即驾驶

## 这一版做了什么

- 普通 `ARaftActor` 需要先建造固定 **船舵 Actor**（`ANavalHelmActor`）并按 `E` 上舵；
  LifeRaft 没有舵台，角色站上其 MovementBase 后自动请求驾驶。
- 上普通木筏后角色被钉在舵位上（`OperatorPoint`），`W/S` 变成进退、`A/D` 变成转向；
  LifeRaft 则由 WASD 直接给出相机空间移动方向、鼠标给出船头朝向，两者互不耦合。
- 再按一次 `E` 下舵：输入还给走路。LifeRaft 会抑制同一筏的自动重进，直到角色真正离开再登筏。
- 舵轮会跟着当前实际转向量转动，作为纯表现反馈。

整条链路与"炮"完全同构，只是把"开火"换成了"操舵"：

| | 重武器 | 船舵 |
| --- | --- | --- |
| 世界里的 Actor | `ANavalHeavyWeaponActor` | `INavalHelmStation`：固定 `ANavalHelmActor` 或 LifeRaft 船体 |
| 谁在用它 | Actor 自己的 `WeaponOperator` | 船上的 `UNavalHelmComponent::Operator` |
| 上/下站位的能力 | `UOceanAdventureGameplayAbility_OperateHeavyWeapon` | `UOceanAdventureGameplayAbility_OperateHelm` |
| 站位期间的输入 | 临时授予 `FireHeavyWeapon` | `OperateHelm` 按船型 push/pop `IMC_OceanHelm` 或 `IMC_OceanDirectHelm` |
| 共同基类 | `UOceanAdventureGameplayAbility_NavalStation` | 同左 |
| 入口 | `InputTag.Naval.Interact`（E） | 普通舵台同左；LifeRaft 由 MovementBase 自动激活 |

## 按下 E 之后发生了什么

```
IA_Naval_Interact (E)
  └─ DA_InputConfig_OceanNaval → InputTag.Naval.Interact
       └─ DA_AbilitySet_OceanNaval 授予的 OperateHelm / OperateHeavyWeapon 同时尝试激活
            ├─ OperateHelm: 一次球形 Overlap 找最近且 CanOperate 的 INavalHelmStation
            │    ├─ 找不到 → 立刻结束（这次按键就当作没有舵）
            │    └─ 找到 → 客户端先本地吸附，同时把 Occupy 请求走 GAS TargetData 发给服务端
            │         └─ 服务端具体舵站重新校验：
            │              舵站有效/距离 + 船体队伍、残骸状态、座位是否被占
            │              拒绝 → EndAbility，客户端的本地吸附随之回滚
            └─ OperateHeavyWeapon: 被 Status.Naval.Steering 挡住（ActivationBlockedTags）
```

LifeRaft 的入口没有绕开这条链：`UOceanAdventureHelmInputComponent` 每 0.1 秒只检查本地
Pawn 的附着父级/MovementBase，不扫世界；发现 `DirectPlanar` 船体后调用 ASC 的
`TryActivateAbilityByClass(OperateHelm)`。随后仍由同一个 Ability 选择舵站、预测吸附并通过
TargetData 请求占用，服务端照常复检唯一 Operator。失败或主动按 E 退出后，同一筏不会高频重试；
离开 MovementBase 后抑制才解除。

上舵成功后：

- `EnterStationPresentation()` 把角色 `AttachToActor` 到船舵上（不是每帧 teleport），
  所以角色天然跟着甲板漂。它不修改 CharacterMovement 的真实 MovementMode。
- 站位能力应用 OceanAdventure 自己的 Infinite `NavalStationLock` GameplayEffect，同时授予
  `Gameplay.MovementStopped` 和当前站位状态标签。CMC、鼠标朝向和 TopDown facing
  都消费这个标签；退出时按 ActiveGameplayEffectHandle 原子移除，ASC 即使已换 Pawn 也不会残留。
- `UOceanAdventureHelmInputComponent::EnableHelmInput(Model)` 压入优先级 2 的对应上下文。
  普通木筏由 `IMC_OceanHelm` 把 WASD 写成 throttle/steer；LifeRaft 由
  `IMC_OceanDirectHelm` 写入 `IA_Ocean_Helm_DirectMove`（Axis2D）。两者都会盖过
  TopDown 原生移动 mapping，输入不会落到角色腿上。
- `OperateHelm` 每 `ControlSampleInterval`（0.05s）从输入组件采样一次，仍旧走
  TargetData 通道发给服务端；
  普通木筏提交 throttle/steer；LifeRaft 先按控制器 Yaw 把 Axis2D 变成世界 XY，并复用
  `GetCursorAimLocation()` 提交朝向目标。服务端按复制的 `MovementModel` 选择解释方式，
  且只接受"它认为正在掌舵的那个 Actor"发来的值，其余一律丢弃。
- 再按一次 `E`：`UAbilityTask_WaitInputPress` 收到同一个输入 → `EndAbility` → 发 Release、
  先弹出 `IMC_OceanHelm`，再移除站位 GE、解除附着，并上 0.45s 的
  `Status.Naval.StationExitLock`。原来的 Walking/Falling/Swimming 状态保持不变。

## 船舵 Actor 的构成

`ANavalHelmActor`（`Plugins/NavalCore/.../Naval/NavalHelmActor.h`）：

| 组件 | 作用 |
| --- | --- |
| `ConsoleMesh` | 舵台底座，**无碰撞**——设计 8.3.1 明确不允许远处一枪打瘫全船 |
| `WheelPivot` / `WheelMesh` | 舵轮，跟随 `GetSteerIntent()` 插值转动，纯表现 |
| `OperatorPoint` | 角色站位（舵台后方、面向船首），`GetOperatorTransform()` 提供给能力 |
| `CoreSeatCollision` | 加固舵芯座：唯一的受击体，也是 Overlap 找站位时命中的体积（`WorldDynamic`）；可装配模块包络为 `160 × 160 × 140 cm` |
| `CorePart` | `UNavalPartComponent`，750 耐久 ≈ 普通墙的 2.5–3 倍 |

它不持有"谁在掌舵"这件事——归属、夺船、控制量全在船上的 `UNavalHelmComponent`，
Actor 只是玩家能走到、能看到、能打到的那一层。

两个 Mesh 默认用引擎基础体素灰盒，所以**没有美术资源也能直接看到并交互**。

## 谁把它放到木筏上

`UNavalHelmComponent` **不再生成舵台**。普通 Raft 的固定舵台是 append-only 建造目录中的
`Raft.Piece.Prop.Helm`，通过 `UBuildStructureVisualComponent` 在服务端生成并 attach 到甲板。
调试命令：`BuildSelect Raft.Piece.Prop.Helm`，再执行 `BuildPlace X Y Level`。

LifeRaft 直接继承 `ARaftVesselActor`，没有 `IBuildStructureHost`、`UBuildStructureComponent`
或 `UBuildStructureVisualComponent`。其 Definition 开启 direct helm interaction，船体本身实现
`INavalHelmStation`，并把 `MovementModel` 设为 `DirectPlanar`；所以站上去会自动走既有 GAS
占用链，但世界中没有一个伪装的船舵 Actor。普通 `DA_Raft_Default` 保持 `Helm`。

两套模型只共用船壳状态、吨位/推力/浮力系数和复制姿态，不共用控制积分：

| 模型 | 平移 | 转向 | 松手与侧滑 |
| --- | --- | --- | --- |
| `DirectPlanar` | 世界 XY 目标速度，不依赖船头 | 有限角速度追鼠标目标 | 高制动、短外推，适合小救生筏 |
| `Helm` | 沿船头施加推力 | 舵效随航速平滑增长，静止时为 0 | 低漂航减速度、低横向阻力，保留木筏惯性与甩尾 |

`BPC_NavalLoad_RaftT0` 还按总吨位计算连续 `LinearResponse` / `AngularResponse`：增加浮筒和
推进只能改善载重/推力分档，不能把已经拼大的木筏重新变成小快艇。

木筏的这套船体组件由 Raft 这个 GameFeature 自己注入（`RaftNaval_AddVesselComponents`）：

```
Raft.uasset (GameFeatureData)
└─ ARaftVesselActor（含派生类）
      ← BPC_NavalVessel_RaftT0 / BPC_NavalLoad_RaftT0
         BPC_NavalHelm_RaftT0 / BPC_NavalMovement_RaftT0
```

`BP_Raft_Helm` 由 `Plugins/GameFeatures/Raft/Content/Python/CreateRaftNavalAssets.py` 生成，
换普通 Raft 的舵轮/舵台模型、微调站位都改这个蓝图。LifeRaft 的直接驾驶站位在
`DA_Raft_LifeRaft` 的 `DirectHelmOperatorLocalOffset/Yaw` 中配置。

## 常见排查

- **普通舵台按 E / LifeRaft 站上后没反应**：先看 `LogOceanAdventure` 里的 `[NavalStation] No station found`。
  多半是普通 Raft 还没建舵台、站得太远（搜索 300cm，舵站校验 260cm），或者
  这条船没被注入 `UNavalHelmComponent`（Raft Feature 未激活/资产脚本未重跑）。
- **上去了立刻被弹下来**：服务端拒绝，日志里会打出 `Server occupy refused`；
  对照舵站的距离/功能状态，以及 `CanOccupy()` 的队伍、残骸、座位占用。
- **上了舵船不动**：先看 `[Helm] Avatar ... has no OceanAdventureHelmInputComponent`、
  `Cannot enable helm input ... tagged native actions are not bound`。出现前者说明
  `OceanNaval_AddHelmInputComponent` 没注入，出现后者说明 PawnData 的
  `NativeInputActions` 没保留三个舵 InputTag；重跑 `CreateOceanAdventureExperience.py`
  后再跑 `CreateNavalP0Assets.py`，并重启编辑器。输入正常但仍不动时，再检查
  `AcceptsControlInput()`（舵芯/夺船/残骸）和推进部件。

## UE 5.7 编辑器 Python 重跑坑点

本次资产迁移遇到的两次失败不是资产损坏，后续修改脚本时应先排除这两项：

- `USceneComponent.set_relative_rotation` 的 UE 5.7 Python 签名不是单参数调用，必须传完整的
  `(rotation, sweep, teleport)`；编辑 CDO 时使用
  `set_relative_rotation(unreal.Rotator(...), False, True)`。
- Unreal Editor 的 Python 解释器会缓存普通 `import`。磁盘源码修正后直接再次 `import`，仍可能
  执行旧 code object；traceback 甚至会显示新文件文本配旧行号。使用
  `importlib.reload(CreateRaftNavalAssets)`，或用 `runpy.run_path(..., run_name="__main__")`
  执行最新文件；仍不一致时重启编辑器。
- `ULyraInputConfig` 在当前 LyraGame 中没有 `LYRAGAME_API` 导出。GameFeature 可以包含其头文件并
  读取公开的 `NativeInputActions` / `AbilityInputActions`，但跨 DLL 调用其非内联成员函数
  `FindNativeInputActionForTag()` 会产生 `LNK2019`。这里按 `InputTag` 本地遍历数组，不为了一个
  查询帮助函数扩大 LyraGame 的导出 ABI。
- `UGameplayTask::ReadyForActivation()` 的 `LNK2019` 表示调用模块没有显式链接
  `GameplayTasks`；只包含 `GameplayAbilities` 头文件不够。这次错误出现在已经撤销的
  `TopDownGameplayAbility_Move` 实验中，因此正确收尾是删除该 Ability 和
  TopDownFeatureRuntime 的多余依赖，而不是为了保留错误分层继续补链接。
- 自主代理应用非 Instant GameplayEffect 时必须显式携带本次 Ability 的预测键。
  仅创建 `FScopedPredictionWindow` 不会改变
  `ApplyGameplayEffectSpecToSelf(Spec, FPredictionKey())` 的默认实参；这里用
  `MakeOutgoingGameplayEffectSpec()` + `ApplyGameplayEffectSpecToOwner()`，同时获得
  Ability/SourceObject effect context 与 `GetPredictionKeyForNewAction()`。监听服务器测不出
  空预测键问题，必须用 Dedicated Server + 独立客户端验证。
- 不要为了能力互斥把 TopDown 的逐帧移动包装成 GameplayAbility。Lyra 有意把 Move/Look
  作为 `NativeInputActions`，位移预测属于 CharacterMovement 的 SavedMove/网络校正链；
  GAS 负责“进入/离开舵位”这个玩家意图和状态标签，不接管 CMC 的预测。
- 不要同时使用 `Gameplay.MovementStopped` 与 `MOVE_None`，更不能退出时硬写
  `MOVE_Walking`；这会把 Falling/Swimming 等真实状态吃掉。站位只应用可复制、可按句柄
  回收的 OceanAdventure GameplayEffect，CMC 在自己的层级把速度和转向归零。不要依赖可空的
  `ULyraGameData::DynamicTagGameplayEffect` 全局配置，否则配置缺失会让上站位直接失败。
