---
name: gameplay-ability-selection
description: 为 OceanAdventure/Lyra 判断新功能是否应实现为 GameplayAbility、应由玩家 ASC 还是世界 Actor/Component 承载，并决定 Ability 的拆分边界。用户询问“什么时候用 Ability/GAS、Ability vs Actor/Component、玩家输入与预测、炮台/交互动作、成功失败/蓄力/释放生命周期、Ability 粒度”时使用；不用于具体 GA C++ 实现、AbilitySet/InputConfig 资产脚本、GAS 运行故障排查或一般 UE 模块架构。
license: Apache-2.0
---

# Game Ability 选型判断

## 先记住一句话

`GameplayAbility` 是“一个可由某个拥有者触发、执行、取消并结束的玩法意图”，不是持久化容器，也不是世界对象的替代品。

推荐的默认分工是：

- **玩家 ASC / GameplayAbility**：玩家发起的输入、目标选择、预测表现、激活/取消/提交/结束生命周期，以及 GAS 的标签、消耗、冷却、GameplayEffect、GameplayEvent 和 TargetData 通道。
- **世界 Actor / Component**：世界中必须存在的对象身份、碰撞/物理、占用关系、耐久、角度/位置、弹药/装填、建造/销毁和其他权威状态。它必须在没有 Ability 实例时仍能被查询和验证。
- **PlayerState / SaveGame / 后端**：跨 Pawn 或跨连接是否保留的数据。断线后是否消失，单独由持久化与重连策略决定，不能直接推出“使用 Ability”。

典型组合是“玩家 Ability 指向一个世界 Actor，并把请求通过 GAS TargetData 送到服务端；世界 Actor 用同一个校验函数复检并写入自己的状态”。不要在 Ability 和 Actor 各维护一份可写的真值。

## 决策流程

按顺序问下面的问题；先命中的强判据优先于“感觉上像技能”。

### 1. 先分离“状态归属”和“动作表达”

把需求改写成两句：

1. **谁拥有状态？** 例如炮的占用者、炮塔角度、装填时间和是否损坏，属于炮 Actor/Component；玩家的技能标签、输入绑定、能力冷却和跨 Pawn 的能力状态，属于玩家 ASC（Lyra 通常把 ASC 放在 PlayerState）。
2. **谁发起动作？** 例如玩家按下“操作/开火”并持续瞄准，是玩家 Ability；炮在服务器上按 AI 逻辑自动射击，则是炮的 Actor/Component 或服务器侧系统发起的动作。

“断线后数据会不会消失”只能回答第一句中的持久化问题，不能回答第二句中的 Ability 选型。

### 2. 命中两个以上强信号时，优先使用 Ability

以下信号直接说明动作适合放在 GAS Ability 上：

- 有一个**玩家可识别的意图**：使用、瞄准、蓄力、冲刺、施法、装填、释放、离开操作位等。
- 需要 **激活 → 进行中 → 提交/命中 → 结束或取消** 的生命周期，或要响应中途输入/事件。
- 需要 Ability 的标准语义：独立授予/移除、激活条件、阻断/取消标签、资源消耗、冷却、GameplayEffect、GameplayEvent、AbilityTask 或失败反馈。
- 客户端要立即做可回滚的表现预测，服务器最终决定是否接受；或者请求天然适合 GAS 的 TargetData 通道。
- 输入来自玩家且应沿 Lyra 的 Enhanced Input → InputTag → ASC → Ability 管线分发。

只有“成功/失败”并不排除 Ability；瞬时攻击同样可以用 Ability 表达。反过来，存在多个阶段也不自动要求 Ability：如果阶段是世界对象的持续状态机，而没有玩家激活、取消、预测或 GAS 语义，Actor/Component 仍应拥有它。

### 3. 判断 ASC 应放在哪里

- **玩家控制的动作**：默认使用玩家已有的 ASC（Lyra 通常是 `ALyraPlayerState` 的 ASC，Pawn 只是 Avatar），Ability 通过 `SourceObject`、TargetData 或接口引用目标 Actor。
- **需要跨 Pawn/换身保留的能力状态**：放在 PlayerState ASC；这仍然是“玩家状态归属”，不是“断线持久化已自动解决”。若要跨断线保留，还要显式接入 SaveGame/后端/重连恢复。
- **世界对象的长期真值**：放在 Actor/Component；例如炮的角度、占用者、装填计时和耐久。不要为了让炮“能开火”就给每门炮创建一个 ASC。
- **自主 NPC/机器人的独立技能**：只有当该对象确实需要 Ability 的授予、标签、消耗、冷却、预测/网络语义时，才为它配置 ASC；否则用 Actor/Component 状态机更直接。

Actor 数量和组件成本是一个有效的工程约束，但不是独立的设计结论：炮本身已经是一个必须存在的 Actor，是否再增加 ASC 要看它是否真的需要 GAS，而不是把 Actor 数量当成“Ability 的定义”。

### 4. 网络预测要检查“本地控制权”，不要背成“预测 key 绑在 PlayerController 上”

更准确的规则是：LocalPredicted Ability 在本地 ASC 上先执行，再把激活/TargetData 请求交给服务器；它要求 ASC 的父 Actor 由本地控制玩家拥有，并且 ActorInfo 的 Owner/Avatar/Controller 链正确。预测 key 属于 GAS 的激活/预测窗口，并不等于“只要把 Ability 放到某个 Actor 上就能预测”。

因此：

- 静态或共享炮的 ASC 没有稳定的玩家网络拥有权时，把鼠标输入和 LocalPredicted Ability 直接放到炮上通常会失败、变成等待服务器或难以安全授权。
- 玩家按鼠标控制炮时，使用玩家 ASC 的 Ability 更自然：Lyra 的 InputTag/Enhanced Input 和本地 PlayerController 都已经在这条链上；Ability 再把炮 Actor 作为目标。
- 若世界 Actor 确实由某个玩家独占并正确设置 Owner/ActorInfo，Actor ASC 也可以做 LocalPredicted；这不是默认情况，必须先验证网络拥有权和双端 `ActorInfo`。

无论放在哪里，客户端输入都只是请求。服务端必须重新执行完整的距离、队伍、占用、冷却、角度、碰撞和资源校验，然后才写入复制状态。

### 5. 决定 Ability 的粒度

以“独立玩家意图/独立网络请求/独立取消边界”为单位，而不是以每个函数或每个动画为单位。

**合并到同一个 Ability**，当这些步骤总是一起发生，且共享激活条件、标签锁、资源/冷却、网络策略和结束条件。例如“按住开火 → 蓄力与预览 → 松开提交 → 结束”通常是一个 Ability。

**拆成不同 Ability**，当步骤可以独立授予或移除，拥有不同的输入/激活条件、冷却/消耗、阻断或取消关系、预测策略，或者可以被不同系统单独触发。例如“操作炮位”和“开火”可以是两个 Ability：前者持有操作状态，后者只在操作标签存在时激活。

**不要因为代码很短就拆分**：独立函数、AbilityTask、纯计算器或 Actor 方法通常足够。可复用的瞄准/弹道/验证逻辑应下沉为框架接口或 Actor/Component 方法，Ability 只编排生命周期和提交路径。

## 你给出的 6 条判断：核实后的版本

| 原判断 | 结论 | 应采用的写法 |
|---|---|---|
| 断线后数据消失 → Ability，否则 Actor | **不成立** | 断线持久化由 PlayerState、SaveGame、后端和重连策略决定；Ability 是动作表达。玩家动作可在 PlayerState ASC 上，世界状态仍在 Actor 上。 |
| 场景 Actor 多，尽量把能力放玩家身上 | **部分成立** | 不要为每个世界对象无理由创建 ASC；但 Actor 仍负责世界身份和状态。用玩家 ASC Ability + 目标 Actor，而不是“Ability 代替 Actor”。 |
| 预测 key 绑在 PlayerController，炮 ASC 不是玩家就预测不了 | **方向对，表述不准确** | 关键是 ASC 父 Actor 的本地网络拥有权和正确 ActorInfo/Controller 链；PlayerController 是常见拥有链的一部分，不是预测 key 的所有者。 |
| 鼠标来自玩家，适合放玩家身上 | **通常成立** | Lyra 输入由玩家 Enhanced Input/InputTag 驱动，玩家 ASC 是默认落点；如果对象被玩家独占且网络拥有权正确，Actor ASC 也不是绝对禁止。 |
| 需要成功/失败/释放中等中间态 → Ability | **部分成立** | “进行中/取消/提交/结束”是强信号；仅有成功/失败不够，瞬时动作也可用 Ability；世界状态机也可能不需要 GAS。 |
| 粒度太细要合并 | **成立但需加边界** | 按独立意图、授权/预测、取消、冷却和输入边界合并；若能独立授予、阻断、取消或触发，就应拆开。 |

## 重炮反例：本项目的推荐形状

OceanAdventure 的重炮实现是可复用模板：

- `ANavalHeavyWeaponActor` 持有炮的世界状态（`WeaponOperator`、`TurretYawLocal`、`NextFireServerTime`、功能/耐久组件），并提供 `CanOperate`、`CanFire`、`TryOccupy`、`TryFire` 等服务端权威入口。
- `UOceanAdventureGameplayAbility_OperateHeavyWeapon` 在玩家 ASC 上处理进入操作位、鼠标取样、持续控制、离开和操作状态标签。
- `UOceanAdventureGameplayAbility_FireHeavyWeapon` 在玩家 ASC 上处理蓄力、轨迹预览、松开提交、失败反馈和 Ability 结束；它通过 GAS TargetData 发送请求，服务端再次调用炮的 `TryFire`。
- 炮的瞄准角度等复制真值由 Actor 写入；客户端预测只更新表现，不能直接授权射击。

可对照：

- [Lyra PlayerState 的 ASC 与 Pawn Avatar](../../../Source/LyraGame/Player/LyraPlayerState.cpp)
- [Lyra AbilitySet 的 InputTag 与 SourceObject](../../../Source/LyraGame/AbilitySystem/LyraAbilitySet.cpp)
- [重炮世界状态与服务端校验](../../../Plugins/NavalCore/Source/NavalCoreRuntime/Public/Naval/NavalHeavyWeaponActor.h)
- [重炮开火 Ability 的 LocalPredicted 与 TargetData](../../../Plugins/GameFeatures/OceanAdventure/Source/OceanAdventureRuntime/Private/Naval/OceanAdventureGameplayAbility_FireHeavyWeapon.cpp)
- [重炮操作 Ability 的玩家鼠标取样](../../../Plugins/GameFeatures/OceanAdventure/Source/OceanAdventureRuntime/Private/Naval/OceanAdventureGameplayAbility_OperateHeavyWeapon.cpp)

## 输出判断时必须说明的 5 件事

对一个新功能给出结论时，至少写清：

1. **动作发起者**：玩家输入、AI、服务器系统还是世界对象自身。
2. **权威状态拥有者**：PlayerState ASC、Pawn/Player ASC、世界 Actor/Component、SaveGame/后端中的哪一个。
3. **是否需要预测**：若需要，列出本地拥有权、Owner/Avatar/Controller 链和回滚边界。
4. **Ability 生命周期与粒度**：激活、进行中、提交、取消、结束；哪些步骤合并，哪些独立。
5. **服务端复检与验证证据**：列出同一校验函数、TargetData/RPC 通道、复制字段和 PIE/Dedicated Server 验证方式。

## 权威参考

- [Epic: Understanding the Gameplay Ability System](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-the-unreal-engine-gameplay-ability-system)
- [Epic: Gameplay Ability System Component and Gameplay Attributes](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-ability-system-component-and-gameplay-attributes)
- [Epic: Using Gameplay Abilities](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-gameplay-abilities-in-unreal-engine)
- [Epic API: FPredictionKey](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/GameplayAbilities/FPredictionKey)

