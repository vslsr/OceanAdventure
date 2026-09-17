# TypeScript 玩法层（PuerTS）验证协议

覆盖：装上 PuerTS 之后，脚本是不是真的接管了战斗与交互，以及**热重载是不是真的免重编**。

本仓库的执行环境没有 UE 工具链，也没有 PuerTS，所以**下面这些没有一条在提交前跑过**。
已经跑过的只有不需要引擎的那三条门禁（`npm run check` 与 `Tools/check_absolute_paths.py`），
它们在第 0 节，是给执行者确认「至少源码这层是自洽的」用的。

## 前置条件

1. **要验的代码在你的分支上。** 见 `AGENTS.md`「修复必须落到 main」：

   ```bash
   git fetch origin main
   git merge-base --is-ancestor <本次提交> origin/main && echo 在 main 上 || echo 还没落地
   ```

   本次改动涉及 **C++ + 新插件 + .uproject + TS 产物**，少任何一半都编不过，
   所以务必整分支检出，不要只挑文件。

2. **PuerTS 已安装。**

   ```bash
   node Tools/setup-puerts.mjs
   ```

   要求打印 `PUERTS_PRESENT`。打印 `PUERTS_MISSING` 时按它列的步骤装完再来——
   **不要跳过这条继续往下跑**：没有 VM 时游戏照样能玩，第 2 节往后每一条都会"通过得很假"。

3. **编辑器已重编。** ScriptCoreRuntime 在**编译期**探测 PuerTS，不是运行期。
   装完插件不重编，跑出来的还是没有 VM 的那个版本。

   ```powershell
   & "$Engine\Engine\Build\BatchFiles\Build.bat" LyraEditor Win64 Development `
       "-Project=$PWD\LyraTemplate.uproject" -WaitMutex -NoHotReloadFromIDE -NoUBA -MaxParallelActions=1
   ```

   `$Engine` 怎么查见 `README.md`。

4. **Node 18+，依赖已装：** 仓库根 `npm install`。

## 执行

### 0. 不需要引擎的门禁

```bash
node Tools/setup-puerts.mjs
python Tools/check_absolute_paths.py
npm run check
```

### 1. VM 起来了

编辑器里开 PIE，输出日志 `Cmd` 模式执行：

```
script.status
```

### 2. 战斗规则接管了

PIE 里用重武器打**敌方**角色一发，再打**己方**角色一发，看输出日志里的 `LogOceanAdventure` /
`LogScriptCore`。友伤那发的最终伤害应当是基础值的约 25%（`src/tuning.ts` 的 `friendlyFireScale`）。

### 3. 热重载，且不重编

**不要关 PIE。** 另开一个终端：

```bash
npm run watch
```

把 `Plugins/GameFeatures/OceanAdventure/TypeScript/src/tuning.ts` 里的
`friendlyFireScale` 从 `0.25` 改成 `1.0`，存盘。等 `watch` 打印出产物已重建，
**回到还在跑的那个 PIE**，再打己方角色一发。

### 4. 交互规则接管了

PIE 里试着搬运一个**敌方队伍**的可搬运物（没有队伍归属的箱子不算）。

### 5. 没有 VM 时不会坏

关掉 PuerTS 再验一次降级路径——把 `Plugins/Puerts/` 临时改名，重编编辑器，开 PIE，
执行 `script.status`，然后正常打一枪。

## 预期

| # | 判据 |
| --- | --- |
| 0 | `PUERTS_PRESENT` / `ABSOLUTE_PATH_CHECK_OK` / `SCRIPT_API_PARITY_OK` + `SCRIPT_DIST_FRESH_OK` 四个标记都出现，退出码都是 0 |
| 1 | `script.status` 打印 `backend=PuerTS running=yes`，并至少列出一个 `bundle ...OceanAdventure/Content/Script/main.js`；日志里有 `[ts:OceanAdventure] scripts bound` |
| 1 | 日志里有 `[ts:OceanAdventure] damage rule bound`（**只在服务端/监听服务端**；纯客户端上应当是 `damage rule not bound: this is a client...`） |
| 2 | 每次命中都出现一条 `[ts:...]` 之外的遥测不强制；**强制的是**：友伤那发的实际扣血明显低于敌方那发（约 1/4），且两发都确实扣了血 |
| 3 | 改完存盘后 **2 秒内**日志出现一次 `[Script] PuerTS started 1 bundle(s)`（hot reload），随后再打己方一发，伤害回到满额 |
| 3 | **整个过程没有任何 C++ 编译，PIE 没有重启。** 这条是本次改动的核心承诺，不满足即为 FAIL |
| 4 | 搬运被拒绝，日志出现 `Carry.Message.Failed refused: Naval.Fail.WrongTeam`；搬自己队伍的同类物件仍然成功 |
| 5 | `script.status` 打印 `backend=Null running=no`，日志里有一条 `No script VM in this build` 级别的错误；**开枪照常扣血，数值是未经脚本修改的基础值** |

## 报告模板

整段贴回，不要转述。

```
状态：PASS / FAIL / BLOCKED

环境
  引擎路径来源：Launcher / 注册表 / 源码自建
  提交：<git rev-parse HEAD>
  在 main 上：是 / 否
  setup-puerts.mjs：PUERTS_PRESENT / PUERTS_MISSING
  编辑器重编：成功 / 失败

0 门禁
  <四条命令的完整输出>

1 VM
  script.status 输出：
  <原样粘贴>

2 战斗
  敌方一发扣血：
  己方一发扣血：
  相关日志行：

3 热重载
  存盘到日志出现 started 的间隔：
  改后友伤扣血：
  期间是否发生 C++ 编译：是 / 否
  期间是否重启 PIE：是 / 否

4 交互
  拒绝日志行：
  同队搬运是否成功：

5 降级
  script.status 输出：
  开枪扣血：

失败时：从第一条报错到结尾的原始输出
<粘贴>
```

**只回「通过」或「有问题」不接受。** 也不要自己筛掉看起来不重要的报错——
判读不是执行者的职责。第一个失败就停下回报，后续失败往往是它的下游。
