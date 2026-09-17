# SkyLand 玩家控制迁移方案

> 目标：把 SkyLand（Vite + TypeScript + Three.js 的 Web 原型）的**玩家控制手感**迁进
> OceanAdventure（UE 5.7 + Lyra），键位与移动数值都按源工程对齐。
>
> 生成日期：2026-09-16
> 源工程：`SkyLand`，主要在 `config/input/player.input.json`、`config/actors/player-slime.actor.json`、
> `shared/physics/stepCharacter.mjs`、`src/controllers/TopDownController.ts`
> 目标：`Plugins/GameFeatures/OceanAdventure/Content/Input/`、`BP_OceanAdventure_Pawn`
> 落地入口：[`MigrateSkyLandPlayerControl.py`](../../Plugins/GameFeatures/OceanAdventure/Content/Python/MigrateSkyLandPlayerControl.py)
> 验证协议：[`doc/test/OceanAdventure-SkyLandPlayerControl.md`](../test/OceanAdventure-SkyLandPlayerControl.md)

---

## 一、结论

**输入这一层几乎不用翻译，数值那一层全靠换算。**

SkyLand 的输入本来就是「IA / IMC / InputConfig」三层声明式结构（`player.input.json`），
字段语义和 Enhanced Input 一一对应——`inputActions` 就是 IA、`inputMappingContexts` 就是 IMC、
`inputConfig.bindings` 就是 `ULyraInputConfig` 的 tag→IA 表。所以这层是**照搬**，不是重写。

数值那一层不同：SkyLand 全程用米，UE 用厘米；而且 `stepCharacter` 的加速模型是
「按恒定速率逼近目标速度」，UE 的 `CalcVelocity` 默认还带一项与速度成正比的摩擦。
两个摩擦项不归零，数值抄对了手感也不对（见下方第三节）。

### 已落地（前四行由脚本一次跑完，最后一行是 C++）

| 层 | 内容 |
| --- | --- |
| IA | `IA_Player_Sprint` / `IA_Player_Interact` / `IA_Player_Drop` |
| IMC | `IMC_OceanPlayerControl`，优先级 2，由 GameFeature Action `SkyLandPlayerControl_AddInputMapping` 注入 |
| InputConfig | `DA_InputConfig_OceanAdventure` 的 Native/Ability 两个数组各加自己那几条 |
| 手感数值 | `BP_OceanAdventure_Pawn` 的 CharacterMovement 九个浮点 + 一个开关 + 可行走坡度 |
| Tag | `InputTag.Player.Sprint` 注册进 `Config/DefaultGameplayTags.ini` |
| C++ | `UTopDownPawnComponent`：冲刺消费方（含服务端 RPC）+ 鼠标朝向改指数收敛 |

### 刻意不搬

| 内容 | 理由 |
| --- | --- |
| WASD、滚轮缩放、右键拖拽转镜头 | `CreateOceanAdventureExperience.py` 已经拥有它们，**键位和 SkyLand 完全一致**。再映射一遍等于一个绑定两个主人，下次谁跑谁赢。脚本改为**审计并报告** |
| 空格跳跃 | `/Game/Input/IMC_Base` 已经映射，键位同样一致。差的只是跳跃**数值**，那在移动组件上 |
| 胶囊体尺寸（半径 0.42 m、高 0.84 m） | SkyLand 的玩家是 0.84 m 的史莱姆，Ocean 的 Pawn 是人形 Lyra 角色。把胶囊缩到史莱姆尺寸改的是碰撞与美术，不是手感 |
| 手柄与触屏映射 | 本次范围是键鼠核心手感。源工程的手柄/虚拟摇杆分支（含 0.12 死区）留在 `player.input.json` 里，要接时按同一张表补 |
| 快捷栏 1-9、四个 AbilityLab 键、装填、背包、翻滚 | OceanAdventure 侧既没有对应 InputTag 也没有消费方。只建资产不接线＝往仓库里加噪声 |
| `airDrag: 0.6` | **源工程里是死数据**：`PlayerMovementComponent` 读了它，但权威模拟 `createCharacterSimulationParams` 从没把它传进 `stepCharacter`。迁移它等于凭空发明一个手感 |
| `maximumStepHeight: 0.2` | 同上，玩家原型里这个值没进模拟。真正生效的是 `characterParams.AUTOSTEP_MAX_HEIGHT = 0.35` |

---

## 二、键位对照

| SkyLand 映射 | 键 | 目标 IA | InputTag | 槽位 | 消费方 |
| --- | --- | --- | --- | --- | --- |
| `Sprint.Keyboard.Primary/Alternate` | 左/右 Shift | `IA_Player_Sprint` | `InputTag.Player.Sprint` | Native | `UTopDownPawnComponent` |
| `Interact.Keyboard.Primary` | F | `IA_Player_Interact` | `InputTag.Ability.Interact` | Ability | Lyra 既有交互 GA |
| `Drop.Keyboard.Primary` | Q | `IA_Player_Drop` | `InputTag.Ability.Quickslot.Drop` | Ability | Lyra 快捷栏 GA（当前 Experience 未必授予） |
| `Move.Keyboard.*` | W/A/S/D | 已有 `IA_OceanAdventure_Move*` | `InputTag.TopDown.Move*` | Native | `UTopDownPawnComponent` |
| 镜头缩放 / 转动 | 滚轮 / 右键 / Mouse2D | 已有 `IA_OceanAdventure_TopDownCamera*` | `InputTag.TopDown.Camera.*` | Native | 同上 |
| `Jump.Keyboard.Primary` | 空格 | 已有 `IA_Jump` | `InputTag.Jump` | Ability | Lyra 跳跃 GA |

**F 是加键不是换键**：`DA_InputConfig_Base` 里的 `E → IA_Interact` 条目原样保留，两条都绑
`InputTag.Ability.Interact`，E 和 F 同时可用。脚本的数组过滤因此按「IA + Tag」两项匹配，
比 `CreateOceanAdventureExperience.py` 的「只按 Tag」更窄——只按 Tag 会把 E 那条删掉。

**优先级 2 的理由**：`IMC_Base` 在 0，`IMC_OceanAdventure_Base` 在 1；同优先级时先注册的赢键。
本 IMC 要抢的 F / Q / Shift 在 `IMC_Base` 里另有用途，所以必须坐在两者之上。

---

## 三、数值对照（米 → 厘米）

源：`config/actors/player-slime.actor.json` 的 `playerMovement` / `playerJump`，
外加 `shared/physics/characterParams.mjs`。

| SkyLand | 值 | UE 属性 | 值 | 换算 |
| --- | --- | --- | --- | --- |
| `walkSpeed` | 3.2 m/s | `MaxWalkSpeed` | 320 | ×100 |
| `sprintMultiplier` | 1.65 | `UTopDownPawnComponent.SprintSpeedMultiplier` | 1.65 | 是比例，不换算；按住时 `MaxWalkSpeed` = 320 × 1.65 = 528 |
| `acceleration` | 28 m/s² | `MaxAcceleration` | 2800 | ×100 |
| `deceleration` | 24 m/s² | `BrakingDecelerationWalking` | 2400 | ×100 |
| —— | —— | `bUseSeparateBrakingFriction` | true | 见下 |
| —— | —— | `BrakingFriction` | 0 | 见下 |
| —— | —— | `GroundFriction` | 0 | 见下 |
| `playerJump.impulse` | 7 m/s | `JumpZVelocity` | 700 | ×100 |
| `playerJump.gravity` | 22 m/s² | `GravityScale` | 2.2449 | 2200 / 980 |
| `airAcceleration` × `airControl` | 8 × 0.85 = 6.8 m/s² | `AirControl` | 0.2429 | 680 / `MaxAcceleration` |
| `AUTOSTEP_MAX_HEIGHT` | 0.35 m | `MaxStepHeight` | 35 | ×100 |
| `MAX_SLOPE_CLIMB_ANGLE` | π/3 | `WalkableFloorAngle` | 60 | 弧度转角度 |
| `maximumFallSpeed` | 20 m/s | —— | —— | 移动组件上没有这个属性，对应的是 `PhysicsVolume.TerminalVelocity`（默认 4000）。想精确对齐要改关卡的 PhysicsVolume |

### 两个摩擦项为什么必须归零

`stepCharacter` 的加速是 `moveVectorTowards(v, target, accel * dt)`——**恒定速率逼近**，
没有任何与当前速度成正比的项。UE 的 `CalcVelocity` 默认还会额外把速度往输入方向「拧」，
拧的强度正比于 `GroundFriction`。两边只有在摩擦归零时才是同一条曲线：

- `GroundFriction = 0`：有输入时只剩 `MaxAcceleration`。满速 180° 回头＝ `2 × 320 / 2800 = 0.23 s`，与 SkyLand 一致；
- `bUseSeparateBrakingFriction = true` + `BrakingFriction = 0`：松手时只剩
  `BrakingDecelerationWalking`。满速刹停＝ `320 / 2400 = 0.13 s`，与 `deceleration 24` 一致。

只抄速度和加速度、留着默认摩擦，跑起来的表现是「起步黏、回头拖」——数值全对，手感不对。

### `AirControl` 为什么不是 0.85

UE 的空中加速度是 `MaxAcceleration × AirControl`，SkyLand 的是 `airAcceleration × airControl`。
两边的 `airControl` 不是同一个量：源工程里它乘的是 8 m/s²，UE 里它乘的是 28 m/s²。
照抄 0.85 会得到 23.8 m/s² 的空中加速度，**比地面加速度还接近满值**，跳起来像在飞。
按结果对齐：`680 / 2800 = 0.2429`。

---

## 四、C++ 侧的两处（已完成）

输入铺到门口还不够，这两处的消费方原本不存在。都落在
`Plugins/GameFeatures/TopDownFeature/Source/TopDownFeatureRuntime/`。

### 4.1 冲刺：`UTopDownPawnComponent` 按住 Shift ×1.65

移动本来就归这个组件，它已经有一套「`EditDefaultsOnly` 的 InputTag + `BindNativeAction`」的模式，
冲刺照着加即可：`SprintInputTag`（默认 `InputTag.Player.Sprint`）+ `SprintSpeedMultiplier`（默认 1.65），
按下/松开改写 `MaxWalkSpeed`。三个细节值得记下来：

- **基准速度只在第一次捕获，且从实例读。** `BaseMaxWalkSpeed` 在 `BeginPlay` 取当前值——那是
  Pawn 蓝图调好的 320。写成 `GetDefault<UCharacterMovementComponent>()->MaxWalkSpeed` 会读到引擎默认的
  600：**冲刺一次、松手一次，蓝图里那份调参就被静默覆盖掉了**，而且现象是「跑得比走还快之后就回不去了」，
  没人会想到去查一个读 CDO 的默认值。
- **必须告诉服务端。** 只在本地改 `MaxWalkSpeed`，服务端仍按 320 模拟，它的位置修正会每一帧把客户端拽回来——
  典型现象是「按住 Shift 一顿一顿的」，看着像网络抖动。加了 `ServerSetSprinting`（`Server, Reliable, WithValidation`），
  组件因此要 `SetIsReplicatedByDefault(true)`，否则 RPC 根本不路由。代价是一个 RTT 的短暂不一致，
  不是持续橡皮筋；要彻底消掉得走 `FSavedMove_Character` 的压缩标志，那是另一个量级的改动。
  其他客户端看到的是复制过来的移动，不需要知道谁在冲刺。
- **冲刺绑定是可选的。** 它刻意留在那段「七个动作缺一就整体禁用」的检查之外：
  `DA_InputConfig_Base` 没有冲刺条目，而 `SimpleExperience` 的 PawnData 还在用它。
  把冲刺算作必需，等于让那个 Experience 的 WASD 和镜头一起失效。

`UnbindInput()` 里会把冲刺清掉——服务端那份组件从不绑定输入，所以取消 possess 时也走这条路复位。

### 4.2 朝向：`FixedTurn` 换成 `RInterpTo`

**这里原来的写法和源工程不是同一条曲线，只改默认值不等价。**
`FacingRotationInterpSpeed` 过去喂给 `FMath::FixedTurn`，语义是**每秒多少度**（恒定角速度）；
SkyLand 用的是 `lerpAngle(current, target, min(1, dt × 10))`——步长是**剩余误差**的一个比例。

差别不是细节：恒定角速度下，180° 大转身要爬 18 秒（按 10°/s），而最后 1° 仍以满速冲过去；
指数收敛则是一开始快、越接近越慢，不会过冲。`FMath::RInterpTo` 的实现正是
`Current + Delta × Clamp(Dt × Speed, 0, 1)`，与源工程同形，所以 `MOVEMENT_FACING_SHARPNESS = 10`
可以原样搬过来。默认值从 `0.0f`（瞬时对齐）改成 `10.0f`；保留 0 = 瞬时对齐的语义。

## 五、幂等与所有权

脚本可以反复跑，规矩和 `CreateOceanAdventureExperience.py` 一致：

- **自己的 IMC 自己清**：先 `unmap_all_keys_from_action` 再 `map_key`，改键位不会留下旧键；
- **InputConfig 按值过滤**：用 `gameplay_tags_equal` + 包路径比较，不用 `in`——
  UE 5.7 的 `FGameplayTag` / UObject 包装器 `==` 比的是包装器身份，用 `in` 过滤会永不命中，
  脚本退化成累加器，跑三次条目变三份（见 `python-script-governance` 的 `PY-LYRA-004`）；
- **重建后断言长度**：`len(最终) == len(保留) + len(新增)`，不靠「新条目在不在里面」判定，
  存在性检查对重复条目一样返回 True；
- **GameFeature Action 按稳定名替换**：`SkyLandPlayerControl_AddInputMapping`，其它 Action 一律保留；
- **数值先体检再写**：九个属性名先全部读一遍，有一个读不到就直接报错退出，不写任何值——
  半套落地的数值比没落地更难发现，它看起来像一个有意为之的状态。
