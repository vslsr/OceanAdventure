---
name: lyra-input-fire-debug
description: 为 OceanAdventure/Lyra 排查和修复“鼠标单击不进 Enhanced Input、双击才触发、按住蓄力或松开发射不稳定”的运行时输入链问题，并调试炮台/GameplayAbility 的近距离预览和最低充能参数。用户提到单击、双击、LeftMouseButton、IA_Naval_Fire、Capture/NoCapture、CommonUI 输入策略、按住充能或松开发射时使用；不用于一般 UMG 设计、AbilitySet/InputConfig/GameFeatureData 资产脚本自动化、Blender 资源或单纯 GAS 架构选型。
---

# Lyra 输入开火排查

用于 OceanAdventure/Lyra 中“按一下没有开始充能、双击才触发、松开不发射”的证据驱动排查。核心目标是先定位输入链的第一个丢失事件，再改最小责任层；不要从 Ability 没有激活反推鼠标或 GAS 根因。

## 适用边界

- 处理 `PlayerInput -> Slate/Viewport -> Enhanced Input -> LyraHeroComponent -> ASC -> GameplayAbility` 的运行时断点。
- 处理 CommonUI `FUIInputConfig` 的输入模式、鼠标捕获和可见光标组合，尤其是游戏视口中的普通鼠标按下。
- 处理玩家拥有的炮台/重武器蓄力 Ability：最低预览距离、最低充能比例、松开提交和服务端重校验。
- 资产脚本只负责该玩法自己的 Cannon Blueprint 覆盖值；AbilitySet、InputConfig、GameFeatureData 的通用 Python 构造改用 `lyra-editor-asset-automation`。
- Ability 与 Actor/Component 的拆分决策改用 `gameplay-ability-selection`；不要把本技能扩展成 GAS 架构指南。

## 先建立可观察链路

按层加日志，日志字段至少包含事件、Action/Tag、Ability spec、SourceObject、角色本地/权威状态和世界时间：

1. 输入装配：记录 `ULyraHeroComponent` 注入的 `ULyraInputConfig`、`InputTag -> UInputAction`、绑定的 `Triggered/Completed` 句柄。
2. 原始鼠标：在 `APlayerController::PreProcessInput` 只做诊断，记录 `WasJustPressed/WasJustReleased/IsPressed`；不得用它驱动玩法。
3. Hero/ASC：记录 `Input_AbilityInputTagPressed/Released`、匹配的 spec、`InputPressed`、激活结果和释放分发。
4. Ability：记录 `OnGiveAbility/OnRemoveAbility`、`CanActivateAbility`、`ActivateAbility`、充能样本、释放回调、TargetData、服务端验证和 `EndAbility`。
5. CommonUI：记录输入策略 Widget 是否 Push 成功，以及 `GetDesiredInputConfig()` 实际被查询到的模式、捕获方式和是否隐藏光标。

只把“日志确实出现/缺失”的边界写入结论。例如：有 `MouseProbe RELEASED` 但没有 `PRESSED`，且没有 `Hero event=Triggered InputTag.Naval.Fire`，只能确定普通按下边沿在 Hero 之前丢失；不能继续猜 `CanActivate` 或充能任务。

## NoCapture 的确定性陷阱

UE 5.7 `FSceneViewport::OnMouseButtonDown` 在游戏视口中只有以下条件之一成立时才把普通按下发送到 `ViewportClient::InputKey(IE_Pressed)`：视口已捕获，或捕获模式为 `CapturePermanently_IncludingInitialMouseDown`，或当前不是游戏视口。`EMouseCaptureMode::NoCapture` 不满足条件，因此可能出现：

- 普通单击没有 `IE_Pressed`，Enhanced Input Action 保持 `None/false`；
- 鼠标释放仍被送达，日志看起来像“只有释放”；
- 双击由 `OnMouseButtonDoubleClick` 单独发送，表现为“双击才触发”。

当目标是“鼠标仍可见、左键按下进入游戏、松开结束充能”时，TopDown 的 CommonUI 策略应使用：

```cpp
FUIInputConfig(
    ECommonInputMode::All,
    EMouseCaptureMode::CaptureDuringMouseDown,
    /*bHideCursorDuringViewportCapture=*/false);
```

`All` 不是这里的根因；关键是 `NoCapture`。不要为修复左键丢失而改成永久隐藏鼠标的 FPS 捕获，也不要在 Tick 中轮询按键。右键拖拽可以在按住 Action 开始后临时 Push 一个独立的永久捕获策略。

## 蓄力与近距离参数

先区分两个配置层：

- Ability 的 `MinimumChargeAlpha` 是初始预览和松开提交的最低充能比例。它应保留小于等于 1 的约束；需要短按近乎立即发射时，将它调到小的正值（例如 `0.02`），并在 `Charge setup` 日志打印实际值。
- Cannon Blueprint 的 `MinimumRange` 是实际最小命中距离，且会被 `CanFire` 和服务端 `TryFire` 复检。它不是 Ability 的充能比例。若要让预览/短射靠近炮口，应在玩法自己的资产脚本中改 Blueprint 覆盖值，并重新编译/保存资产。

修改后验证日志应能把两者同时读出来，例如：

```text
[NavalFire] Charge setup ... min_charge_alpha=0.020 min_range=400 ...
```

不要只改通用 `NavalCore` 父类默认值来改变某一个 GameFeature 的 Cannon；优先改 `OceanAdventure` 自己的 Blueprint 配置脚本，避免跨 GameFeature 产生隐式行为。

## 二元判据与验证

### 输入断点

- 单击时有 `MouseProbe PRESSED`、`Hero Triggered Fire`、`NavalFire Activate`：输入链已通，继续检查 Ability/充能。
- 只有 `MouseProbe RELEASED` 或 Action 为 `None`：先检查 CommonUI 捕获模式、游戏视口焦点和实际 Mapping Context；不要修改 Fire Ability。
- Action 有 `Triggered`，但 ASC 没有匹配 spec：检查临时授予/移除时序和 InputTag；不要猜网络丢包。

### 充能断点

- 有 `Activate`、`Charge setup` 和递增 `Charge sample`：按住充能正常。
- 有 `Input released` 但没有 `Submit shot`：检查 Ability 自身释放任务和 `InputPressed` 状态。
- 有 `Submit shot` 但 `Local CanFire refused` 或服务器 `TryFire` 拒绝：以日志中的具体 `FailReason`、武器 SourceObject、范围和权威状态为准。

### 编辑器/PIE 验证

1. 编译包含改动的 `TopDownFeatureRuntime`/`OceanAdventureRuntime` 模块并重启编辑器。
2. 若改了 Cannon 资产脚本，重跑脚本或直接编辑 `BP_Naval_GroundCannon` 的覆盖属性，编译并保存；Blueprint 覆盖值优先于 C++ 父类默认值。
3. PIE 中按 E 进入炮位，然后只按一次并保持左键；确认 `PRESSED -> Triggered -> Activate -> Charge setup`，松开确认 `Completed -> Input released -> Submit/TargetData`。
4. 另外测试右键拖拽；确认独立的 `RotateHold Started/Completed` 与相机输入没有回归。
5. 若使用 Pixel Streaming，必须再用本地 PIE/Standalone 窗口做一次对照；流媒体连接日志只能作为隔离变量，不能替代输入事件证据。

当前环境无法代替 UE 编译和 PIE；交付时明确列出未编译部分和需要用户在编辑器执行的步骤。
