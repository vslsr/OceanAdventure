# 台阶地形测试协议

覆盖 `OceanCore` 的地形系统（迁移方案 P1–P3）。分两部分：

- **A. 自动化测试**——5 条，机器判定，有明确成功标记；
- **B. 编辑器目视与行走验收**——人判定，但每条都给了可一眼判定的判据。

B 不能被 A 代替：A 证明数据和拓扑对，**证明不了画面朝向对、碰撞建成了**。

相关：[`../tech/SkyLand_地形系统迁移方案.md`](../tech/SkyLand_地形系统迁移方案.md)（设计与各阶段验收）、
[`../../.agents/skills/ue5-debug-validation/references/build-failure-triage.md`](../../.agents/skills/ue5-debug-validation/references/build-failure-triage.md)（编译失败的三个签名）。

---

## 前置条件

四条都满足才开始。任一条不满足，回报 `BLOCKED` 并注明是哪条。

1. 当前分支包含完整地形实现。检查：

   ```powershell
   git ls-files "Plugins/OceanCore/Source/OceanCoreRuntime/*/Terrain/*" | Measure-Object -Line
   ```

   **应为 23 行。** 少于 23 说明分支不完整——见 `AGENTS.md`「源码变更必须整套落地」，
   这时 `git pull` 不解决问题。

2. 编译通过。**测试入口不负责构建**——缺模块、编译失败或旧 DLL 都会让结果失真。
   上一次编译失败过的话，先删缓存再编：

   ```powershell
   if (-not $env:UE_ROOT) { throw '请先设置 UE_ROOT 为本机 UE 安装根目录' }
   Remove-Item -Recurse -Force .\Intermediate, .\Plugins\OceanCore\Intermediate -ErrorAction SilentlyContinue
   & (Join-Path $env:UE_ROOT 'Engine/Build/BatchFiles/Build.bat') LyraEditor Win64 Development "-Project=$PWD\LyraTemplate.uproject" -WaitMutex -NoHotReloadFromIDE -NoUBA -MaxParallelActions=1
   ```

   预期末尾 `Result: Succeeded`。

3. 夹具文件在：

   ```powershell
   Test-Path .\Plugins\OceanCore\Source\OceanCoreRuntime\Private\Terrain\Tests\Fixtures\terrain-parity.txt
   ```

   **应为 `True`**（约 334 KB）。只有 A-1 用它。

4. 按路线准备宿主：
   - **走 A（命令行，推荐）**：设好 `UE_ROOT`，并关掉已打开的编辑器，避免两个进程抢同一份项目状态；
   - **走 A′（编辑器内控制台）**：编辑器已启动且**不在 PIE 中**（没点 Play）；
   - **走 B（目视验收）**：必须是完整编辑器，且**不能用 `-NullRHI`**。

---

## A. 自动化测试（命令行）

按 [`ue5-commandline-automation`](../../.agents/skills/ue5-commandline-automation/SKILL.md) 走。
这 5 条都是纯 C++ 的数据与拓扑断言，不依赖 GPU、截图或渲染，所以 `-NullRHI` 适用。

先切到仓库根目录：

```powershell
if (-not $env:UE_ROOT) { throw '请先设置 UE_ROOT 为本机 UE 安装根目录' }
$project = (Resolve-Path -LiteralPath './LyraTemplate.uproject').Path
$ue = Join-Path $env:UE_ROOT 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $ue -PathType Leaf)) { throw 'UE_ROOT 下找不到 UnrealEditor-Cmd.exe' }
$commonArgs = @('-Unattended', '-NullRHI', '-NoSound', '-NoSplash', '-stdout', '-FullStdOutLogOutput')
```

### A-0 发现：5 条都注册上了吗

```powershell
& $ue $project '-ExecCmds=Automation List;Quit' @commonArgs
"ListExitCode=$LASTEXITCODE"
```

输出里搜 `OceanCore.Terrain`，应当**恰好 5 条**：

```
'OceanCore.Terrain.CellCode'
'OceanCore.Terrain.Mesh'
'OceanCore.Terrain.Outline'
'OceanCore.Terrain.Parity'
'OceanCore.Terrain.Patches'
```

少任何一条 → `BLOCKED`，贴出搜到的行。**这是「没编进去」，不是「测试失败」**，
两者的下一步完全不同：前者查模块加载与编译条件，后者查算法。
`Automation List` 只发现、不执行；**退出码 0 不代表发现到了目标测试**。

### A-1 执行

```powershell
$runId  = [guid]::NewGuid().ToString('N')
$report = Join-Path (Split-Path $project -Parent) "Saved/AutomationReports/$runId"
& $ue $project '-ExecCmds=Automation RunTests OceanCore.Terrain;Quit' @commonArgs "-ReportExportPath=$report"
"ExitCode=$LASTEXITCODE Report=$report"
```

分号必须留在**同一个** `-ExecCmds` 参数里；另发一条退出命令会把测试截断。
每次用独立报告目录，免得把上次的报告当成这次的结果。

### 成功判据（四条全中才算过）

1. 退出码 **0**；
2. `$report` 目录**已生成**；
3. 报告里**实际执行数 > 0**；
4. 5 条**全部完成且无失败**。

**零匹配、跳过、未运行都不算通过。** 只看退出码会把「一条都没跑」读成成功——
这正是上一轮真实踩到的：`Automation RunTests` 不带参数时报
`No automation tests matched ''`，却照样 `Automation Test Queue Empty`。

超时同样属于未完成，不能算过。

### A′ 备用：编辑器内控制台

编辑器已经开着、只想快速看一条时用。输出日志切 `Cmd` 模式：

```
Automation List
Automation RunTests OceanCore.Terrain.Parity
```

**命令必须带 `Automation RunTests ` 前缀**——裸测试名不是控制台命令，会被静默丢弃。
判据是消息日志出现 `测试"<名字>"完成，结果为"成功"`；**没有这一行就等于没通过**，
哪怕没有任何报错。这条路线拿不到报告目录和执行计数，所以**只用于排查，不作为验收证据**。

### 每条保什么

失败时用来判断影响面：

| 测试 | 它保的东西 |
| --- | --- |
| `Parity` | 两颗种子、`[-64,64]²` 共 **33282 格地形码 + 33282 格群系**逐格对上参考实现，外加 7 条哈希与 10 条噪声探针 |
| `CellCode` | 位打包与符号扩展、出生区平坦、采样高度与角点一致、水体语义（低于海平面的干洼地不积水） |
| `Outline` | 折边判定：平格接平格不出线、坡脚接平地是折边、同向坡并排与顺坡阶梯不出线 |
| `Mesh` | 顶面投影面积**精确等于** `ChunkGrid² × CellSize²`、法线全朝上且崖面竖直、chunk 接缝包含性 |
| `Patches` | 编辑语义（改回默认值清空覆盖、抬高雪地仍是雪地、紧邻水域下挖进水而孤立深坑不进水）、通知集合 == 实际网格变化集合 |

### `Parity` 失败要分三种报

它是唯一读磁盘夹具的，三种原因完全不同，**贴回时照抄报错首行**：

| 报错形如 | 含义 |
| --- | --- |
| `cannot read parity fixture at <路径>` | 路径解析问题，与算法无关。把它报的那个路径一起贴回 |
| `Hash32(...) = ... , expected ...` 或 `ValueNoise(...)` | 整数哈希移植有问题。它会直接返回不往下跑，**地形图的结果不用看** |
| `CellCodeAt(...) = xxxx (h=.. s=.. b=.. shape=..), expected ...` | 真的对不上。最多打印前 8 处，末尾给总数——**两样都要贴** |

---

## B. 编辑器目视与行走验收

A 全绿之后再做。这部分验的是 A 验不到的：朝向、接缝、碰撞。

### B-0 前置：把地形组件挂上

现在**没有任何东西**把地形组件挂到 chunk actor 上，不挂就什么都看不到。

1. 打开 `/OceanAdventure/Blueprints/BP_OceanWorldManager`，看 `Ocean | Chunk → Chunk Class`。
   若指向 C++ 类 `OceanChunkActor`，改成 `BP_OceanChunk_Debug`（纯 C++ 类挂不了组件）。
2. 打开 `BP_OceanChunk_Debug` → Add Component → 搜 **Ocean Terrain Chunk** → 加上 → 编译保存。
   不用连任何蓝图节点，组件自己会订阅 chunk 初始化。
3. 可选：把组件的 `Fill Material` 设成 `/LineArtCore/Materials/M_LineArt_Fill`。
   不设也能验几何，只是地形是灰的。

> 内容浏览器看不到 `/OceanAdventure/` 或 `/LineArtCore/` 时，
> 在右上角 Settings 里勾 **Show Plugin Content**。

### B-1 逐条检查

打开 `/OceanAdventure/Maps/L_OceanChunkTest`，Play。

| # | 检查 | 判据（能一眼判定） | 不满足说明什么 |
| --- | --- | --- | --- |
| B-1 | 输出日志有无 `ChunkSize ... terrain grid is 32 cells` 的 **Error** | **没有** | 有资产把 `ChunkSize` 覆盖成了非 6400 |
| B-2 | 地形可见 | 从上方能看到起伏地面 | **完全看不见、或只有从地下往上才看得到 = 绕序反了**（迁移方案坑 #2） |
| B-3 | 世界原点 | 有一块**约 22m × 22m 的平地**，再往外才起伏 | 平地不存在说明真相层没在驱动渲染 |
| B-4 | chunk 接缝 | 走到地形块交界处，**没有裂缝、没有错位台阶** | 采样窗口或邻块通知有问题 |
| B-5 | 斜坡 | 角色能**走上**斜坡，不是被挡住也不是穿过去 | 碰撞没建成，或只建了顶面 |
| B-6 | 崖面 | 一米落差的垂直面**挡住**角色 | 同上 |
| B-7 | 墨线（可选） | 勾组件的 `bDrawInk` **再 Play**，能看到折边（深色）与崖线（红色） | 勾了没反应见下方注意 |

> **B-7 注意**：`bDrawInk` 目前只在构建完成时决定是否开 tick，
> 所以**运行中临时勾不生效**，必须 Play 之前就勾上。

---

## 报告模板

整段复制，填完贴回。**不要删掉没跑到的行**——写 `未执行` 比省略更有用。
A 段的字段按 [`ue5-commandline-automation`](../../.agents/skills/ue5-commandline-automation/SKILL.md)
的交付要求列，少任何一项都无法判断「过了」是不是真的过了。

```text
## 环境
分支/提交：
地形文件数（应 23）：
编译结果：
夹具存在（应 True）：
引擎版本：

## A 自动化测试（命令行）
过滤条件：OceanCore.Terrain
退出码：
报告目录：
实际执行数（须 > 0）：
通过 / 失败 / 跳过：   /   /
A-0 List 里看到的 OceanCore.Terrain 条目（应 5 条）：

逐条结果：
  Parity    ：PASS / FAIL / 未执行
  CellCode  ：PASS / FAIL / 未执行
  Outline   ：PASS / FAIL / 未执行
  Mesh      ：PASS / FAIL / 未执行
  Patches   ：PASS / FAIL / 未执行

失败项的原始输出（从第一条报错到结尾，照抄不转述）：
```

```text
## B 编辑器验收
B-0 组件已挂 / Chunk Class 指向：
B-1 无 ChunkSize Error ：是 / 否
B-2 地形可见           ：是 / 否（否：完全看不见 / 只有从下面看得到）
B-3 原点有 22m 平地    ：是 / 否
B-4 接缝无裂缝         ：是 / 否
B-5 能走上斜坡         ：是 / 否
B-6 崖面挡住角色       ：是 / 否
B-7 墨线可见（可选）   ：是 / 否 / 未测

异常截图或日志：

## 整体状态
PASS / FAIL / BLOCKED（BLOCKED 时注明卡在哪一条前置）

## 验证等级
VERIFIED（真实宿主执行）/ STATIC_ONLY（只看了源码或命令，未实际运行）
```
