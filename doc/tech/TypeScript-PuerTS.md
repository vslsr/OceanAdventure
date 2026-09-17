# TypeScript 玩法层（PuerTS 接入）

战斗数值、交互判定、反馈表现这些**一天要改三遍**的规则，搬到 TypeScript 里写；
下面的弹道、权威、GAS、复制照旧留在 C++。目标只有一句话：

> **改一条规则 = 存一次文件，不是重编一次引擎。**

这条承诺有边界，先把边界说清楚，后面每一节都在守它：

| 你要做的事 | 代价 |
| --- | --- |
| 改伤害公式、衰减曲线、拒绝条件、放哪个特效 | `npm run build`，编辑器里连 PIE 都不用重启 |
| 给脚本一个它现在够不到的能力（新 UFUNCTION、新转发通道） | 一次 C++ 重编 |
| 换引擎版本 / 换 PuerTS 版本 | 只动一个文件，见「PuerTS 只出现在一个文件里」 |

脚本调的是**反射出去的 C++ 接口**，不是引擎内部。它没法自己发明一个不存在的接口——
所以第二行那个代价没有办法绕过去，也不该绕。

## 分层

```
Plugins/ScriptCore/                     通用框架：只管 VM、脚本根、热重载
  Source/ScriptCoreRuntime/             UScriptEnvSubsystem / UScriptEventBus
  TypeScript/                           框架 TS + 引擎 typings + 三个门禁脚本

Plugins/GameFeatures/OceanAdventure/    玩法层：战斗与交互的「下层接口」
  Source/.../Public/Script/             Hooks / 三个 ScriptLibrary / MessageBridge
  TypeScript/src/                       规则本体（这层是策划改的）
  Content/Script/main.js                编译产物，随 Feature 一起打包
```

依赖方向和仓库其它地方一致（见 `AGENTS.md`「模块分层」）：
**ScriptCore 不认识 LyraGame、GAS、CommonUI，也不认识任何 GameFeature**。
它只知道「有一个 VM，有若干脚本根，文件变了要重跑」。
战斗和交互长什么样，全部归玩法层。

两个 GameFeature 之间照旧**不许互相依赖**——这条在 TS 侧是靠打包方式落实的，
不是靠自觉：每个 Feature 打成一个自包含的 IIFE，互相 `import` 不到对方（见下）。

## 脚本怎么被跑起来

1. `UScriptEnvSubsystem`（GameInstance 子系统）在**地图加载完成**时启动 VM。
   不是在子系统 `Initialize` 里——那时候世界还不存在，而脚本要绑的 Hook、Bridge 都是
   World 子系统。在那里启动的话现象是「handler 一次都不触发」，并且不报任何错。
2. 它扫描脚本根：项目的 `Content/Script/`，加上**每个已启用插件**的 `Content/Script/`。
   不用配置，Feature 把自己的脚本放在自己目录里就会被找到。
3. 每个根里的 `main.js` 被读出来、各自套一层 `try/catch`、拼成一段程序交给 VM。
   套 `try/catch` 是为了**一个 Feature 的脚本炸了不会带走别人的**。
4. 脚本在启动时把自己的规则绑到 Hook 上。绑完就完事——它不做任何清理。

热重载走同一条路：文件一变，`tsc`/esbuild 产物落地 → 目录监视（仅编辑器）防抖 0.4 秒 →
整个 VM 拆掉重建 → 上面 1-4 再跑一遍。

**为什么是整个 VM 重建，而不是只换一个模块**：闭包。热重载会生成新的闭包，
旧的那批如果还挂在 C++ 的 delegate 上，一次事件就会触发 N 遍（重载 N 次之后）。
那个现象看起来像数值算错了，不像脚本系统坏了，排查方向会整个跑偏。
所以宿主在拆 VM 之前先广播 `OnScriptEnvStopping`，**所有持有脚本 delegate 的 C++ 都在那里解绑**，
这件事不交给脚本作者去记。

## 两条通道，别用错

| 通道 | 载荷 | 用在哪 |
| --- | --- | --- |
| `UOceanAdventureScriptHooks` / `UOceanAdventureScriptMessageBridge` | **带类型**，能传真的 `AActor*` | 每次命中都要走的判定、要指名道姓说「对谁」的反应 |
| `UScriptEventBus`（ScriptCore） | JSON 字符串 | 没有 UObject 的东西：阶段切换、遥测计数、脚本之间互相通知 |

JSON 那条每次事件要序列化一次，而且丢掉对象身份；换来的是**通道加字段不用动 C++**。
带类型那条反过来：签名一改就要重编，但每帧/每次命中都跑得起。

两条通道都**只用 delegate 属性订阅**，不把 delegate 当 UFUNCTION 参数传：
「把一个 JS 函数当参数塞进 UFUNCTION」是脚本绑定层各版本差异最大的地方，
而「订阅一个 delegate 属性」是所有版本都一致的写法。这不是风格偏好，是为了少踩版本雷。

## 战斗：钩子挂在哪、为什么挂在那

`UOceanAdventureNavalDamageRelay::OnProjectileImpact` 里，在**框架已经判定打中了谁**之后、
**GAS 被告知扣多少血**之前。

上游（弹道、墙窗规则、是谁开的枪、服务端权威）全在 C++，脚本碰不到；
脚本只拿到一个 `FOceanAdventureDamageContext` 快照，返回一个 `FOceanAdventureDamageVerdict`。

三条硬规矩：

- **没绑规则 = 原样返回 BaseDamage。** 没装 PuerTS、bundle 语法错、策划正在改——
  这三种情况下游戏必须和没有脚本时一模一样。「伤害悄悄不结算了」比「脚本没生效」糟得多。
- **伤害规则只在服务端跑。** 绑的时候和调用的时候都查一遍：监听服务器可能在还没确定自己
  是什么的时候就绑上了。
- **交互规则两端都跑。** 这条和上面相反，是故意的：交互是**请求**，客户端要用同一份规则做预测，
  服务端再复检一遍。只在服务端跑的话，每一次拒绝都会变成一次拉回。

交互侧现在挂在 `UOceanAdventureGameplayAbility_Carry::CanCarryTarget` 的**成功路径**上，
所以脚本**只能收紧、不能放宽**——「架在船上的炮不能徒手搬」这种必须守住的规则留在 C++ 里。

## PuerTS 只出现在一个文件里

`Plugins/ScriptCore/Source/ScriptCoreRuntime/Private/Backend/ScriptBackend_Puerts.cpp`。

整个仓库里只有它 `#include "JsEnv.h"`，而且只用三个调用：构造 `FJsEnv`、`Start(源码)`、析构。
模块解析、热重载、事件通道全部在 Unreal 这边自己实现——那些是脚本 VM 的 API 里
**版本间差异最大**的部分，也是最不值得让它散进整个工程的部分。

换 PuerTS 版本、或者哪天换成别的 VM，改动范围就是这一个文件加 `ScriptBackend.h` 那个接口。

没装 PuerTS 时编进来的是 `ScriptBackend_Null.cpp`：它**启动就报错**，而不是安静地什么都不做。
理由同上——一个「看起来在跑」的降级是最难发现的故障。

V8 调试器（inspector）**没有接**：它要 `FJsEnv` 的扩展构造函数，而那个签名连带模块加载器和
logger，正好是版本差异最大的三样。要开的话就在这个文件里换构造函数，别在别处绕。

## 手写 typings 与它的门禁

脚本要调 C++，就得有 `.d.ts`。PuerTS 能从活着的反射生成一份完整的 `ue.d.ts`——但那要求
**这台机器装了编辑器和插件**。所以仓库里放的是手写的那份：

- `Plugins/ScriptCore/TypeScript/typings/ue-core.d.ts`
- `Plugins/GameFeatures/OceanAdventure/TypeScript/typings/ue-oceanadventure.d.ts`

手写的好处不只是「没编辑器也能 typecheck」，更重要的是它**把边界写下来了**：
脚本被允许够到什么，是一份能 review 的清单，而不是「那天反射恰好暴露了什么」。

手写的代价很具体：**过期的声明会让一个运行时不存在的调用通过编译**，
然后游戏什么都不说（规则没绑上就退回 C++ 默认值）。所以有门禁：

```bash
npm run check:api     # node Plugins/ScriptCore/TypeScript/tools/check-api-parity.mjs
```

它读 `Public/Script/` 下的头文件，把 UFUNCTION / UPROPERTY / USTRUCT 按**名字和参数个数**
和 `.d.ts` 对一遍。C++ 有、typings 没有 → 报错；typings 有、C++ 没有 → 也报错，
而且**后面这个方向才是要命的那个**，它就是那个凭空捏造的调用。

只对名字和参数个数，不对类型——`FString` 映射成什么是绑定生成器的事，
在这里重新实现一遍它的意见，只会得到一个以另一种方式出错的检查。
名字和参数个数覆盖的是真正会发生的漂移：加了个参数、改了个名、删了个函数。

只有 **Blueprint 可见**的成员算边界（`BlueprintCallable`/`BlueprintPure`/`BlueprintReadWrite`/
`BlueprintAssignable`…）。脚本 VM 其实连私有成员都够得到，但把私有字段写进契约，
等于以后每次重构内部实现都变成一次脚本 API 变更。

`npm run check:api -- --list` 看当前认到了哪些类型和成员。

## 编译产物为什么进版本库

`Plugins/**/Content/Script/main.js` 是提交的。理由和 typings 那条同源：
**引擎里没有 Node**。只有源码的检出等于没有脚本，而没有脚本不会报错——
每条规则退回 C++ 默认值，游戏照常跑，打包也照常出。

代价是产物会和源码脱节，所以：

```bash
npm run check:dist    # 重新构建一遍，和提交的那份逐字节比
```

打包侧还要一行：`.js` 不是资产，cooker 的引用图看不见它。
每个带脚本的插件在自己的 `Config/FilterPlugin.ini` 里列出 `/Content/Script/main.js`。

## 日常怎么用

```bash
npm install                  # 一次就够，workspaces 会把两个 TS 工程都装上

npm run watch                # 边改边出：产物落地 → 编辑器自动重载 VM
npm run build                # 出一次产物
npm run check                # typecheck + check:api + check:dist，提交前跑这条
```

编辑器控制台（不写菜单路径，理由见 `AGENTS.md`「按 UE 5.7 的实际情况写」）：

| 命令 | 作用 |
| --- | --- |
| `script.status` | 打印 backend（`PuerTS` / `Null`）、脚本根、已加载的 bundle |
| `script.reload` | 手动重跑一遍，不等文件监视 |
| `script.eval <js>` | 在活着的 VM 里跑一段，用来试探当前状态 |

**第一次拿到这个仓库**：`node Tools/setup-puerts.mjs`。
它只报告装没装、怎么装，不会替你下载——替人把几百 MB 的插件下到一个猜出来的路径里，
结局通常是一个解压到一半的插件加一句两边都没提到的编译错误。

## 设置项

项目设置 → Game → **Script Host (TypeScript)**（`UScriptHostSettings`）：

| 项 | 默认 | 说明 |
| --- | --- | --- |
| `EntryModule` | `main` | 每个脚本根里要跑的文件名（`main.js`） |
| `bAutoStart` | 开 | 每次地图加载完自动起 VM |
| `bEnableHotReload` | 开 | 仅编辑器；关掉之后只能 `script.reload` |
| `HotReloadDebounceSeconds` | 0.4 | 一次 `tsc` 会连写好几个文件，第一个就重载会跑到半成品 |
| `AdditionalScriptRoots` | 空 | **相对项目**的额外根；不写绝对路径 |

## 已知边界

- **一个进程里跑多个 GameInstance**（单进程多客户端 PIE）时，每个 GameInstance 有自己的 VM。
  脚本在宿主调用它的过程中拿到的一定是对的那个（`FScopedScriptCall` 保证）；
  在那之外（比如 VM 自己的定时器回调里）拿到的是最后启动的那个，并且会打一条警告。
  多客户端验证请用**分进程**。
- **按路径加载资产**（`SpawnEffectAtLocation` / `PlaySoundAtLocation`）不进 cooker 引用图。
  只被脚本引用到的资产要靠 AssetManager 规则保住，否则打包后加载失败——会打日志，不会静默。
- **inspector 没接**，见上。

## 相关

- 验证协议：[`doc/test/ScriptCore-PuerTS.md`](../test/ScriptCore-PuerTS.md)
- 插件总览：[`doc/tech/Plugins-Overview.md`](Plugins-Overview.md)
