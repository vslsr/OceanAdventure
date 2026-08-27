# 船舵（主舵台）：按 E 上舵、再按 E 下舵

## 这一版做了什么

- 普通 `ARaftActor` 需要先建造固定 **船舵 Actor**（`ANavalHelmActor`）；LifeRaft 没有舵台，走近船体按 `E` 直接驾驶。
- 上舵后角色被钉在舵位上（`OperatorPoint`），`W/S` 变成进退、`A/D` 变成转向，控制的是船体而不是脚。
- 再按一次 `E` 下舵：输入还给走路，船保持航向并自然减速（不急停、不自动驾驶）。
- 舵轮会跟着当前实际转向量转动，作为纯表现反馈。

整条链路与"炮"完全同构，只是把"开火"换成了"操舵"：

| | 重武器 | 船舵 |
| --- | --- | --- |
| 世界里的 Actor | `ANavalHeavyWeaponActor` | `INavalHelmStation`：固定 `ANavalHelmActor` 或 LifeRaft 船体 |
| 谁在用它 | Actor 自己的 `WeaponOperator` | 船上的 `UNavalHelmComponent::Operator` |
| 上/下站位的能力 | `UOceanAdventureGameplayAbility_OperateHeavyWeapon` | `UOceanAdventureGameplayAbility_OperateHelm` |
| 站位期间的输入能力 | 临时授予 `FireHeavyWeapon` | 临时授予 `DriveHelm`（同一 Spec 带两个舵 InputTag） |
| 共同基类 | `UOceanAdventureGameplayAbility_NavalStation` | 同左 |
| 输入 | `InputTag.Naval.Interact`（E） | 同左 |

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

上舵成功后：

- `EnterStationPresentation()` 把角色 `AttachToActor` 到船舵上（不是每帧 teleport），
  所以角色天然跟着甲板漂；`MOVE_None` + `Gameplay.MovementStopped` 停掉走路。
- `Gameplay.MovementStopped` 会立即结束四个方向中正在运行的 `TopDownGameplayAbility_Move`，
  并阻止它们在站位期间重新激活；`UTopDownPawnComponent` 不再直接绑定 WASD。
- 服务端占位成功后给同一玩家 ASC 临时授予 `DriveHelm`。该 Spec 的 `SourceObject` 是舵站，
  并带 `InputTag.Naval.Helm.Throttle/Steer`；Ability 压入优先级 2 的 `IMC_OceanHelm`。
- `DriveHelm` 每 `ControlSampleInterval`（0.05s）从 Config 对应的 Enhanced Input Action
  采样一次，仍旧走 TargetData 通道发给服务端；
  服务端 `SetControlIntent()` 只接受"它认为正在掌舵的那个 Actor"发来的值，其余一律丢弃。
- 再按一次 `E`：`UAbilityTask_WaitInputPress` 收到同一个输入 → `EndAbility` → 发 Release、
  先撤销临时 `DriveHelm`（自动弹出 `IMC_OceanHelm`），再解除附着，并上 0.45s 的
  `Status.Naval.StationExitLock`。

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
`INavalHelmStation`；所以按 E 能开船，但世界中没有一个伪装的船舵 Actor。

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

- **按 E 没反应**：先看 `LogOceanAdventure` 里的 `[NavalStation] No station found`。
  多半是普通 Raft 还没建舵台、站得太远（搜索 300cm，舵站校验 260cm），或者
  这条船没被注入 `UNavalHelmComponent`（Raft Feature 未激活/资产脚本未重跑）。
- **上去了立刻被弹下来**：服务端拒绝，日志里会打出 `Server occupy refused`；
  对照舵站的距离/功能状态，以及 `CanOccupy()` 的队伍、残骸、座位占用。
- **上了舵船不动**：`AcceptsControlInput()` 为假（舵芯被打坏 / 正被夺船 / 船已成残骸），
  或临时 `DriveHelm` 没有拿到 Config/IMC。先看 `LogOceanAdventure` 中
  `[Helm] Granted DriveHelm`；若随后出现 `[DriveHelm] Tagged input assets are unavailable`，
  重跑 `CreateNavalP0Assets.py` 并重启编辑器。确认 `DA_InputConfig_OceanNaval` 的
  `AbilityInputActions` 有两个舵 InputTag，且旧的 `OceanNaval_AddHelmInputComponent` Action
  已被脚本移除。舵本身不产生动力；没有推进部件时只使用船体基线。

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
- 不要把 TopDown 移动或舵输入注册成常驻组件的 `NativeInputActions` 再直接调用
  `AddMovementInput` / `SetControlIntent`。这种实现虽然键能触发，却绕过 ASC 的 Ability
  激活/阻止/取消链，`Gameplay.MovementStopped` 也无法真正禁掉玩家 WASD。本次迁移把
  TopDown 四方向放入 `AbilityInputActions + DA_AbilitySet_TopDownMovement`，把舵控制放入
  临时 `DriveHelm` Spec；Pawn/船组件只作为执行器和服务端状态所有者。
- GameFeature 使用 `UAbilityTask_*` 时，`GameplayAbilities` 依赖并不会替当前模块链接
  `UGameplayTask` 的实现。若调用 `ReadyForActivation()` 后出现该符号的 `LNK2019`，需要在
  模块 `Build.cs` 中显式加入 `GameplayTasks`；本项目将它放在
  `TopDownFeatureRuntime` 的 `PrivateDependencyModuleNames`。
