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

### 已落地（脚本一次跑完）

| 层 | 内容 |
| --- | --- |
| IA | `IA_Player_Sprint` / `IA_Player_Interact` / `IA_Player_Drop` |
| IMC | `IMC_OceanPlayerControl`，优先级 2，由 GameFeature Action `SkyLandPlayerControl_AddInputMapping` 注入 |
| InputConfig | `DA_InputConfig_OceanAdventure` 的 Native/Ability 两个数组各加自己那几条 |
| 手感数值 | `BP_OceanAdventure_Pawn` 的 CharacterMovement 九个浮点 + 一个开关 + 可行走坡度 |
| Tag | `InputTag.Player.Sprint` 注册进 `Config/DefaultGameplayTags.ini` |

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
| `Sprint.Keyboard.Primary/Alternate` | 左/右 Shift | `IA_Player_Sprint` | `InputTag.Player.Sprint` | Native | ⚠️ **待接**，见第四节 |
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
| `sprintMultiplier` | 1.65 | —— | (528) | ⚠️ 无消费方，见第四节 |
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

## 四、还没接线的两件事（都要 C++，不在本脚本范围）

脚本把输入铺到了 ASC/组件门口，但这两处的**消费方**还不存在。不写清楚，跑完会以为已经生效：

1. **冲刺（`InputTag.Player.Sprint`）没有消费方。** 按 F/Q 会进 ASC，按 Shift 目前什么都不会发生。
   自然的落点是 `UTopDownPawnComponent`——移动本来就归它，它已经有一套
   `EditDefaultsOnly` 的 InputTag + `BindNativeAction` 模式。加法是：一个 `SprintInputTag`
   字段、`Input_SprintStarted/Completed` 两个处理函数，按下把 `MaxWalkSpeed` 乘 1.65、松开除回去。

2. **鼠标朝向的平滑速度还是「瞬时对齐」。** SkyLand 用
   `lerpAngle(current, target, min(1, dt × 10))`，`UTopDownPawnComponent::FacingRotationInterpSpeed`
   的默认值是 `0.0f`（0 表示直接 snap）。两者公式同形，改成 `10.0f` 就等价。
   它是原生类的 `EditDefaultsOnly` 默认值，**Python 改不了也存不下**（组件是
   `GameFeatureAction_AddComponents` 按原生类注入的，CDO 改动不落盘），只能改 C++ 构造函数。

两件都落在 `Plugins/GameFeatures/TopDownFeature/`，一次编译能一起带走。

---

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
