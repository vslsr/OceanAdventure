# 搬运系统 V1（按 B 抬起 / 放下）

## 这一版做了什么

- 站在已架设的野战重武器旁按 `B`：武器附着到角色的搬运点，整个 Actor 关闭碰撞。
- 再按一次 `B`：武器放到角色正前方的地面上，恢复碰撞；站在落点里的角色被水平轻推开。
- 一次只能搬一个：手上有东西时 `B` 只会执行"放下"。

## 为什么不进背包

设计文档把"搬运"定义成物理态，不是存储态：

- `doc/designer/Naval-Battle-Royale-Game-Design.md` L1016：搬运高级核心时角色双手被占用。
- 同文 7.10 地面直接架设规则：玩家搬运套件时可以移动，但速度降低且不能使用轻型武器。
- 同文 L749：重武器生成实体采购箱，必须搬到船上并按正常施工时间安装。

放进背包会同时删掉暴露、减速和"掉在地上能被敌人抢"这三项代价，还要把耐久、归属、装填
状态序列化成物品才能保住。背包留给资源和消耗品（Lyra 的 `ULyraInventoryManagerComponent`）。

## 分层

| 层 | 位置 | 内容 |
| --- | --- | --- |
| 通用框架 | `Plugins/CarryCore/` | `UCarryableComponent`、`UCarrierComponent`、`CarryGameplayTags`。只依赖 Engine，不认识 GAS、Lyra 和任何 GameFeature |
| 玩法 | `Plugins/GameFeatures/OceanAdventure/.../Carry/` | `UOceanAdventureGameplayAbility_Carry`、输入标签、失败消息，以及所有"这门炮能不能被抬"的游戏规则 |

`ANavalHeavyWeaponActor` 本身不认识搬运：它只是在 `PreInitializeComponents` 里把自己注册成
GameFramework 组件接收者（与 `AOceanChunkActor` 同一写法），组件由 GameFeature 注入。
没启用该 Feature 的 Experience 里，炮就是抬不动的。

## 组件装配

```
OceanCarry_AddComponents (GameFeatureData)
├─ AOceanAdventurePawn      ← UCarrierComponent
└─ ANavalHeavyWeaponActor   ← UCarryableComponent
OceanCarry_AddInputMapping  ← IMC_OceanCarry (B)
OceanCarry_AddInputBinding  ← DA_InputConfig_OceanCarry (InputTag.Carry)
DA_AbilitySet_OceanCarry    ← 由 DA_OceanAdventure_PawnData 授予
```

由 `Content/Python/CreateOceanAdventureCarryAssets.py` 幂等生成，Action 名字固定，重跑只替换
自己的条目。

## 复制模型

真值只有一个：`UCarryableComponent::Carrier`（外加放下时的 `RestLocation` / `RestYaw`）。
服务端写，客户端在 `OnRep` 里跑**同一个** `ApplyCarryState()`，自己完成挂载 / 卸载 / 碰撞开关。

不走附着复制的原因：`ANavalHeavyWeaponActor` 明确 `SetReplicateMovement(false)`（地面炮不动，
甲板炮跟着宿主），而 `AttachmentReplication` 是随移动复制一起收集的，因此挂载和落点必须显式复制。

`UCarrierComponent` 不复制任何东西，它的 `Carried` 缓存由 carryable 的状态在每台机器上推导，
保持单向。

请求走 GAS TargetData（`FOceanAdventureCarryTargetData`），客户端发的是请求不是授权，服务端用
同一套 `CanPickUp` / `CanCarryTarget` 完整复检；放下的落点完全由服务端计算，客户端不参与。

## 放下时的碰撞处理

1. 落点 = 角色脚底 + 前方 `PutDownForwardDistance`（默认 240cm：胶囊半径约 42 + 炮体碰撞盒 80 + 余量），
   再向下做一次线检取地面。
2. 先清 `Carrier` → 各端 `ApplyCarryState()` 落位并恢复碰撞。
3. 再扫 `PutDownClearanceRadius`（默认 130cm）内的 Pawn，用 `LaunchCharacter` 水平轻推
   （默认 320 cm/s，不给 Z）。方向取"炮 → 角色"的水平分量；正好站在中心时退回角色背后方向。

推开是兜底，不是主要手段：默认落点本来就把角色留在占地之外。

## 已知边界与下一版

- 没有搬运前摇 / 撤收时间（设计要求 3–5 秒撤收），没有移动惩罚，没有"搬运时不能用轻武器"。
- 放下不检查地面合法性（坡度、悬崖、深水、队伍预算）。下一版应把
  `UOceanAdventureGameplayAbility_DeployHeavyWeapon::CanDeployHere` 抽成共用静态函数，
  和幽灵预览一起接到放下路径上。
- `DeployHeavyWeapon` 的队伍预算用 overlap 统计，被扛起的炮没有碰撞因而不计数；
  抬着一门炮时理论上可以多架一门。等落下路径接上预算校验时一并解决。
- 搬运中没有状态 GameplayTag。Lyra 的 ASC 在 PlayerState 上、跨死亡存活，
  松散标签需要配套的死亡清理，等移动惩罚一起做。
- 甲板炮（挂在 vessel 上）直接拒绝，理由标签 `Carry.Fail.Mounted`；它应该走建造系统拆卸。
