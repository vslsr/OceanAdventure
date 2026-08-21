# 项目规范

## Blender 脚本

- 所有 Blender Python 脚本统一放置在项目根目录下的 `blender/script/python/` 目录中。
- 除非任务明确要求，否则只生成脚本，不启动、连接或操作 Blender。

## Lyra 参考项目

- `../LyraStarterGame` 是完整的 Lyra 项目，可作为功能、内容资产和实现方式的参考。

## GameFeature 内容归属

- 严禁将属于某个 GameFeature 概念的任何内容放置在该 GameFeature 目录之外。
- GameFeature 的 C++ 代码、Blueprint、内容资产、DataAsset、材质、地图、配置、Python 脚本及其他专属资源，必须放置在 `Plugins/GameFeatures/<GameFeatureName>/` 对应目录下。
- 不得将 GameFeature 专属内容放入项目 `/Game` 内容目录、项目根 `Source/` 或其他插件目录。
- 只有经确认与任何单一 GameFeature 无关、可被多个系统共享的通用能力，才可放入项目或通用插件目录。

## 模块分层

三层，依赖只能自上而下，**同层之间不得互相依赖**：

| 层 | 位置 | 允许依赖 |
| --- | --- | --- |
| 通用框架 | `Plugins/<Name>Core/`（如 `OceanCore`、`BuildingCore`） | 只有 Engine 与其它通用插件 |
| 宿主/内容 | `Plugins/GameFeatures/<Feature>/` | 通用框架 |
| 玩法 | 拥有玩家 Pawn 的那个 GameFeature（本工程是 `OceanAdventure`） | 通用框架、LyraGame |

- 通用框架插件**不得**依赖 `LyraGame`、`GameplayAbilities`、`CommonUI`、任何 GameFeature。
  判据：作弊命令、存档恢复、编辑器工具都不经过 GAS，它们必须能直接调用框架 API。
- **GameFeature 之间不得产生依赖**，包括 C++、资产引用和编辑器 Python 脚本里的互相调用。
  需要共享的能力下沉为通用插件，需要跨越的差异用接口（如 `IBuildStructureHost`）表达。
- 玩法层（GameplayAbility、输入资产、UI Widget、玩家组件注入）归玩法 GameFeature，
  不要放进通用框架，也不要放进宿主 GameFeature。

## Lyra 实现规范

违反下列任何一条都会让功能脱离 Lyra 的既有系统（设置界面、重绑定、UI 栈、预测与回执），
即使当下能跑也必须改。

### 输入

- 禁止硬编码按键：不得出现 `EKeys::` 判断，或在 Tick 里 `WasInputKeyJustPressed` 轮询。
- 一律 Enhanced Input：`UInputAction` + `UInputMappingContext` + `ULyraInputConfig` 的
  `AbilityInputActions` / `NativeInputActions`，通过 `InputTag` 分发。
- 能力由 `ULyraAbilitySet` 授予（PawnData 或装备），输入绑定由
  `GameFeatureAction_AddInputBinding` / `AddInputContextMapping` 注入。

### 鼠标与输入模式

- 禁止读写 `APlayerController::bShowMouseCursor` / `SetShowMouseCursor()`，
  也不要自己保存恢复光标状态 —— 会与 CommonUI 的 ActionRouter 争夺控制权。
- 鼠标可见性、捕获模式、输入路由一律由激活中的 `UCommonActivatableWidget` 的
  `GetDesiredInputConfig()`（`FUIInputConfig`）声明，push/pop widget 让配置自动回退。
  详见 `doc/Lyra-Mouse-Input-Mode.md`。

### 网络与玩法提交

- 玩家发起的玩法请求走 GAS 的 TargetData 通道
  （`CallServerSetReplicatedTargetData` + `AbilityTargetDataSetDelegate`），
  参考 `ULyraGameplayAbility_RangedWeapon`；**不要**在组件上自造 `Server`/`Client` RPC。
- 客户端送上来的一律视为请求而非授权，服务端必须用同一个校验函数完整复检。
- 状态真值只允许服务端写入，客户端只在表现层预测（蒙太奇、音效、幽灵），
  **不要预测复制型数据结构**。
- 失败与事件反馈用 `UGameplayMessageSubsystem` 广播，不要用 Client RPC 单播，
  否则 UI、音效、任务系统无法解耦订阅。

### 组件与装配

- 能力性组件用 `GameFeatureAction_AddComponents` 注入，不要在 Actor 构造函数里
  `CreateDefaultSubobject` 焊死 —— 否则未开启该玩法的 Experience 也会带着它。
  只有构成该 Actor 本体、离开它就无意义的组件才可以是默认子对象。
- 跨系统查找宿主/目标时用 `UWorldSubsystem` 注册表，
  **禁止每帧 `TObjectIterator` 或 `GetAllActorsOfClass` 遍历全世界**。

## 复制（FastArray）

- 写入后必须 `MarkItemDirty(Entry)`，删除后必须 `MarkArrayDirty()`，
  再 `ForceNetUpdate()`（若改过休眠还要 `FlushNetDormancy()`）。
- 复制回调（`PostReplicatedAdd/Remove/Change`）只做"标脏"，把重建合并到下一帧执行，
  不要逐条重建索引与表现。
- 网络索引表（如 `UBuildPieceCatalog`）**只能追加**，不得插入或删除中间项。
- 复制结构里不要直传资产指针，用索引或 `FPrimaryAssetId`。

## 表现与性能

- 不要每帧 `DestroyComponent` + `NewObject`/`RegisterComponent` 重建组件，
  用 `SetVisibility(false)`；反复注册会重建渲染状态并表现为闪烁。
- 材质切换只在状态真正跳变时执行，并对高频翻转的判定加迟滞。
- 半透明预览材质不要 `disable_depth_test`（会盖住整个场景），
  用微小 Z 抬升或 CustomDepth 解决 z-fighting。
- 角色的 MovementBase（通常是根碰撞组件）尺寸只在真正变化时才改，
  且**不要移动根组件**来适配不对称内容 —— 会让整个 Actor 瞬移。

## 网格与尺寸

- 网格对齐由宿主按自身可建区推导（格数、半格偏移、层高基准），
  不要把宿主厚度、甲板尺寸之类的数值硬编码进每个资产的偏移里。
- 尺寸不是格边长整数倍时，余数留作不可建的视觉边缘，宁可少一格也不要让内容悬空。

## 编辑器 Python 脚本

- 脚本必须幂等，可反复运行修复资产。
- 脚本生成的 `UGameFeatureAction` 要指定稳定名字，重跑时替换同名 Action，
  不得追加重复项，也不得清掉用户在编辑器里手工配置的 Action。
- 资产的 `GameplayTag`、`InputTag` 等查找键必须在脚本里显式赋值，
  否则按 tag 查找的运行时代码永远匹配不到。
- `FGameplayTag` 的 `TagName` 没有暴露给 Python，**不能** `unreal.GameplayTag(tag_name=...)`。
  必须用 `unreal.GameplayTagLibrary.request_gameplay_tag(unreal.Name(...), False)` 从注册表取，
  并用 `tag == unreal.GameplayTag()` 判断是否未注册；读回时用相等比较，不要读 `tag_name`。

## 交付与验证

- 本仓库的执行环境没有 UE 工具链。改动 C++ 后**必须明确说明代码未经编译**，
  并列出需要在编辑器里执行的步骤（编译哪些模块、重跑哪些 Python 脚本、如何在 PIE 验证）。
- 涉及复制的改动，验证步骤要覆盖 Dedicated Server + 至少两个客户端。

