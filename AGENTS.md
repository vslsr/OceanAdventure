# 项目规范

## 绝对路径禁令（硬性）

**仓库内任何文件都不得写死绝对路径**——不写盘符、不写某台机器的检出位置。
代码、脚本、文档、注释、提交信息一视同仁。

理由是这条已经反复付过代价：`D:\UEPrj\...`、`C:\EpicWkspc\...`、`E:\...`
在同一个仓库里同时存在过，换台机器就整条命令跑不起来，而报错（`不会被识别为 cmdlet`、
`Could not load Python file`）看上去都不像路径问题。

按目标位置分四种写法：

| 目标 | 写法 |
| --- | --- |
| 仓库内的文件 | 从脚本自身位置推导。Blender 脚本住在 `<project>/blender/script/python/`，`Path(__file__).resolve().parents[3]` 就是工程根 |
| UE 资产 | 用 `/GameFeature/...` 虚拟包路径，不用磁盘路径 |
| 编辑器 Python 入口 | 用模块名 `import`，UE 已把每个启用插件的 `Content/Python` 放进 `sys.path`，不需要也不应该给路径 |
| 仓库外（引擎安装、参考工程） | **没有相对形式**。只能走环境变量或本机探测，**且不给默认值** |

最后一行是重点：给一个机器相关的默认值，只会把「你没告诉我它在哪」变成很久以后
一句莫名其妙的「这个路径不存在」。宁可当场 fail-fast，说清楚要设哪个环境变量。

`__file__` 不存在时（Blender 文本块、内联探针）**停下来报错**，不要猜一个路径兜底——
把产物导到别处比导不出去更难发现。

两处例外，改动时不要顺手「修正」：

- `.agents/skills/python-script-governance/references/error-ledger.md` 里引用的**原始报错文本**
  （含引擎源码路径 `D:\build\++UE5\Sync\...`）是证据，必须逐字保留；
- `.agents/vendor/` 与 `.agents/plugins/` 是第三方资料，不归本仓库改。

**怎么检查**：

```bash
python Tools/check_absolute_paths.py        # 有违规则退出码 1，干净时打印 ABSOLUTE_PATH_CHECK_OK
python Tools/check_absolute_paths.py --list # 看当前放行了哪些、为什么
```

在此之前这条禁令没有任何自动检查，这正是它反复复发的原因：一份文档从另一份抄了命令，
源头后来改好了，抄件留在原地。上面那个例外里「原始报错文本」的豁免是按 **fenced 代码块**
实现的——证据整块放行，而散文里那种会被人照抄的入口命令照样拦。
确属新例外的，在脚本的 `ALLOW` 里加一条并写明理由，别改判定规则。


## 源码变更必须整套落地（硬性）

**一次 C++ 改动的全部文件同进同出**——`.cpp` 和它的头、被改的 `Build.cs`、新增的模块依赖，
要么一起进 main，要么都别进。**禁止用 GitHub 网页版的「Create / Edit file」往 main 单独放源文件。**

这条已经付过代价：`76b5c25` 用网页版把一个 `OceanTerrainMeshBuilder.cpp` 单独放上 main，
它依赖的 22 个地形源文件还在功能分支上。后果不是「少一个文件」那么直白：

- main 直接编译不过；
- 报错是 `C1083: 无法打开包括文件 "Terrain/OceanTerrainMeshBuilder.h"`，
  **看上去像本地没拉全**。于是排查方向跑去 `git pull`、查子模块、查工作区是否干净——
  全都白费，因为远端 main 上本来就没有那个文件；
- 拉取者无从判断，只能一层层验证 HEAD、origin/main、实时远端三者是否一致。

为什么网页版特别容易犯：它一次只能提交一个文件，而且直接落在 main 上，绕过了
「把分支整体合过去」这个天然的完整性保证。想快速把某个文件放上去时，代价由下一个拉代码的人付。

正确做法：在功能分支上完成，整分支合并到 main。

**没有自动检查，这条只能靠人守。** 与绝对路径禁令不同——那条能靠扫描判定，
而「某个 `#include` 指向的是引擎头还是缺失的项目头」，在仓库内是分辨不出来的
（引擎头本来就不在仓库里）。与其加一个会误报、然后被所有人忽略的门禁，不如把
症状写清楚：见 `.agents/skills/ue5-debug-validation/references/build-failure-triage.md`
的第 3 个签名，那里给了 30 秒分辨「不完整落地」与「陈旧 Intermediate」的命令。

## 交给别人执行的验证必须带协议（硬性）

自己跑不了的验证——要开编辑器、要真机、要 GPU——交给别人（可能是另一个 agent）执行时，
**必须同时给出协议文档**，不能只在对话里说一句「帮我跑一下 X」。

协议四节，缺一不可：**前置条件 / 执行 / 预期 / 报告模板**。
执行命令不写编辑器菜单路径（见上一节）。

结果只有三种状态，且：

- `PASS` —— 附成功标记那几行；
- `FAIL` —— 附**原始输出**，从第一条报错到结尾，不转述、不节选「关键部分」；
- `BLOCKED` —— 前置条件不满足，注明是哪一条。

**不接受只回「通过」或「有问题」**：前者无法复核，后者无法定位。
**执行者不负责判读**——不要自行筛掉看起来「应该不重要」的报错，筛掉的往往正是根因。

骨架、模板与实例见 [`doc/test/`](doc/test/)，新协议照 `doc/test/README.md` 的四节写。

为什么定成硬性：没有协议时，回报会停在「我跑了，没问题」这一层，于是要多问一轮；
更糟的是执行者替你做了判读。**这条已经跑通过一次**——2026-09-16 的地形验证按
`doc/test/OceanCore-Terrain.md` 执行，回报里带了退出码、失败数与未运行数，
文档据此直接更新，没有追问。反例同样具体：在此之前一次 `Automation RunTests` 漏了参数，
日志同时出现 `No automation tests matched ''` 和 `Automation Test Queue Empty`——
**只看退出码会把「一条都没跑」读成成功**，而协议要求的「实际执行数 > 0」正好拦住它。

## 按 UE 5.7 的实际情况写，不要凭 UE4 经验（硬性）

本工程是 **UE 5.7 + Lyra**。UE4 的做法在不少地方已经不成立，而**失效的那部分通常照样
「看起来很合理」**——它不会报错，只会让照做的人找不到、或者调用到一个不存在的签名。
所以凡是写进文档、脚本或注释的具体位置与签名，都要以 5.7 的实际情况为准，不能凭印象。

### 1. 编辑器 UI 路径不要写进文档

菜单位置在大版本间会挪。把「菜单 → 子菜单 → 某项」写死在文档里，等于给读者一条
会过期的指令，而且过期时的表现是「按你说的找不到」，不是「报错」。

**改写成不依赖 UI 布局的入口**：

| 目的 | 不要写 | 写这个 |
| --- | --- | --- |
| 跑自动化测试 | `窗口 → 开发者工具 → Session Frontend` | 输出日志 `Cmd` 模式执行 `Automation List` / `Automation RunTests <名字>`；或 `UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunTests <过滤>"` |
| 跑编辑器 Python | 菜单入口 | Output Log 的 **Python 输入模式**执行模块 `import`（见 Python 门禁与 `PY-UE-002`） |
| 指资产 | 磁盘路径 | `/PluginName/...` 虚拟包路径 |

确实必须描述 UI 时（比如「内容浏览器要先开 Show Plugin Content 才看得见插件内容」），
描述**那个开关做什么**，而不是它在第几层菜单里。

**已发生**：文档里写了 `Window → Developer Tools → Session Frontend → Automation`——
那是 UE4 的位置，UE5 已把它挪走，用户在 5.7 的窗口菜单里逐项找不到。
更糟的是这条错误指引先出现在对话里、又被复制进
`doc/line-style-helper/OceanWorldManager.md`，一处错传成了两处。
现已改为控制台与命令行两种入口。

### 2. API 签名不凭记忆，按当前仓库或反射确认

优先级：**本仓库已成功调用的写法 > 引擎源码/反射 > 官方文档 > 记忆**。
记忆里的便利重载在 5.7 可能不存在，失败档案里已有两条实例
（`PY-UE-001` 猜单参数重载、`PY-UE-007` 猜 `get_static_materials`）。

### 3. 反射宏与 `.generated.h` 按 5.x 的行为理解

`UCLASS()` 展开成**按行号命名**的宏，由 `.generated.h` 定义——所以改动 include 块会
移动行号，陈旧的 `Intermediate/` 就会报出看起来像语法错误的编译失败。
三种会误导人的编译签名及处理见
`.agents/skills/ue5-debug-validation/references/build-failure-triage.md`。

## Python 脚本强制门禁

- 创建、修改、审计、排查或准备执行仓库内任何 Python 脚本时，**必须先使用**
  `.agents/skills/python-script-governance/SKILL.md`（`python-script-governance`）。
- 每次编写或修改 Python 之前，必须完整阅读该 Skill 及其
  `references/error-ledger.md`；审计脚本时同样必须先读，不得只搜索当前关键词。
- Python 出现 `Traceback`、`Error`、未处理 `Exception`、`RuntimeError` 或宿主报告脚本未执行时，
  必须在下一次脚本修改前把原始错误、根因、预防规则和验证状态写入失败档案。
  同一错误复发时更新原记录的次数与复发原因，不得用新条目掩盖重复失败。
- 此 Skill 是项目级前置门禁，必须与适用的领域 Skill 组合使用：Lyra 编辑器资产脚本继续使用
  `lyra-editor-asset-automation`，Blender 脚本继续使用 `blender-asset-workflow`，Raft 船体资产族继续使用
  `raft-hull-asset-workflow`。领域 Skill 不能替代失败档案门禁，门禁也不能替代领域实现规范。
- 不得手工编辑 `.uasset` 二进制来绕过脚本错误；没有在 Unreal、Blender 等真实宿主中运行的修复只能标记为
  `STATIC_ONLY`，不能声称已验证。
- **交付任何 Python 脚本时，脚本下方必须附一段不超过五行的「引擎验证」**：产出什么、在哪看、
  怎么算成功（脚本结尾的稳定成功标记）。资产脚本最常见的故障是静默不执行，没有这三行，
  拿到脚本的人只能靠猜。细则见门禁 Skill 的「交付规范」一节。

## Blender 脚本

- 所有 Blender Python 脚本统一放置在项目根目录下的 `blender/script/python/` 目录中。
- 新建或导出的 Blender 原始模型文件（如 `.blend`、`.fbx`、`.obj`、`.glb`）统一放置在项目根目录下的 `blender/models/` 目录中，禁止新增到 GameFeature 的 `ArtSource/`；历史文件可按需迁移。
- Unreal 导入后的 `.uasset`、材质、Blueprint 等运行时资源仍归属对应的
  `Plugins/GameFeatures/<GameFeatureName>/Content/`；编辑器脚本从 `blender/models/` 读取原始文件并导入到该 GameFeature 的 Content 路径。
- 除非任务明确要求，否则只生成脚本，不启动、连接或操作 Blender。

## Lyra 参考项目

- `../LyraStarterGame` 是完整的 Lyra 项目，可作为功能、内容资产和实现方式的参考。

## GameFeature 内容归属

- 严禁将属于某个 GameFeature 概念的任何内容放置在该 GameFeature 目录之外。
- GameFeature 的 C++ 代码、Blueprint、内容资产、DataAsset、材质、地图、配置、Python 脚本及其他专属资源，必须放置在 `Plugins/GameFeatures/<GameFeatureName>/` 对应目录下。
- 不得将 GameFeature 专属内容放入项目 `/Game` 内容目录、项目根 `Source/` 或其他插件目录。
- 只有经确认与任何单一 GameFeature 无关、可被多个系统共享的通用能力，才可放入项目或通用插件目录。
- 两个 GameFeature 需要用同一份资产时，把资产下沉到通用插件的 `Content/`，不要在各自目录里各存一份。
  先例：`/NavalCore/Blueprints/Cannon/BP_Naval_Cannon` —— 野战架设（OceanAdventure）与甲板建造（Raft）共用的那门炮。
  通用插件要装内容需在 `.uplugin` 里打开 `CanContainContent`，其内容只能引用 Engine 与其它通用插件。

## Skill 目录（统一入口）

- **所有 Skill 的唯一真相是 `.agents/skills/<skill-name>/SKILL.md`**，一个 Skill 一个目录，平铺，不再按
  `SKILL/`、`UE5-Skills/skills/`、上游包自带的 `.trae/skills/` 各存一份。
- 各工具按自己的约定读取：
  - Codex / 通用 Agent：原生读 `.agents/skills/`，无需额外配置。
  - Claude Code：`.claude/skills/<skill-name>` 是指向 `../../.agents/skills/<skill-name>` 的软链。
  - Trae：`.trae/skills/<skill-name>` 同样是指向 `../../.agents/skills/<skill-name>` 的软链。
- **不要往 `.claude/skills/`、`.trae/skills/` 里放真实文件**，那里只允许软链；新增或删除 Skill 后跑一次：

  ```bash
  bash .agents/sync-skill-links.sh
  ```

  脚本按 `.agents/skills/` 重建各工具目录里的软链，遇到真实目录会报警告而不是默默覆盖。
- **插件自带的 Skill**（`.agents/plugins/<plugin>/skills/<name>/`）也在 `.agents/skills/` 里露出，但是**软链**：
  那个目录归插件的 sync 脚本所有，它会按远端 catalog 把对不上的条目 `rm -rf`，所以不能把真实 Skill
  搬进去，也不能把插件的 skills 目录指向 `.agents/skills/`——下次开会话就被删光。
  `sync-skill-links.sh` 负责挂载，并在插件那边删掉 Skill 后清理断链。
- 上游 Skill 包里非 Skill 本体的资料（课程讲义、许可证、示意图、包自带的校验脚本）放在
  `.agents/vendor/<包名>/`，只作参考，不参与 Skill 加载。

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

