---
name: ue5-commandline-automation
description: 为 OceanAdventure/UE 5.7 使用 UnrealEditor-Cmd 在不连接已打开编辑器的情况下列出和运行 Automation 测试，导出报告并判断真实结果。用户提到 Automation List、Automation RunTests、无界面测试、命令行自动测试或 CI 测试入口时使用。普通编译报错和玩法故障用 ue5-debug-validation；Python 资产生成用 python-script-governance 及领域技能；不负责编写测试逻辑、打包或搭建多客户端测试环境。
---

# UE 命令行自动化测试

## 适用范围与验证状态

无需连接现有编辑器或 MCP；启动独立的 `UnrealEditor-Cmd.exe` 加载项目和测试模块。
它仍运行 UE 编辑器宿主，不是脱离引擎的单元测试进程。`Automation List` 仅发现测试，
`Automation RunTests` 才执行测试。此入口不使用 `-run=pythonscript`。

本技能的命令已对照 UE 5.7 `Engine/Source/Developer/AutomationController/Private/AutomationCommandline.cpp`
及 [Epic 命令行测试说明](https://dev.epicgames.com/documentation/unreal-engine/run-automation-tests-in-unreal-engine) 核对。
项目命令尚未实际运行，状态为 `STATIC_ONLY`；不得把本技能当作测试通过的证据。

## 执行前检查

1. 先成功构建当前源码的 `LyraEditor Win64 Development`。测试入口不负责构建；缺失模块、
   编译失败或旧 DLL 均会使结果失真。编译问题转 [ue5-debug-validation](../ue5-debug-validation/SKILL.md)。
2. 先按 [本机目录发现](../ue5-local-directory-discovery/SKILL.md) 定位当前项目及匹配引擎：优先显式 `UE_ROOT`，未配置时查询 Launcher 与注册表；无硬编码默认值。
   不把某台机器的盘符或检出路径保存进文件。
3. 从测试注册宏查明名称、所在模块和上下文标记，确认模块实际加载。
   GameFeature 测试不能假定插件已激活；按测试所需 Experience/地图加载。
4. `-NullRHI` 只用于不依赖 GPU、截图或渲染结果的测试。
   渲染测试移除此参数，并提供所需图形运行环境；依赖窗口或输入的测试另行确认宿主能力。
5. 授权运行测试后仅选择相关过滤范围。仅要求记录方法时不启动测试；
   不自动结束用户的编辑器进程、清理构建目录或提交代码。

## PowerShell 入口

下面是交互式命令。先完成本机目录发现，在同一进程中取得 `$project` 并设置临时 `$env:UE_ROOT`。
若以后提取为脚本，仓库根必须从脚本自身位置推导，不能继续依赖启动目录。

```powershell
if (-not $env:UE_ROOT) { throw '请先按本机目录发现技能搜索并确认引擎' }
if (-not $project -or -not (Test-Path -LiteralPath $project -PathType Leaf)) { throw '请先发现并确认项目文件' }
$ue = Join-Path $env:UE_ROOT 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $ue -PathType Leaf)) { throw 'UE_ROOT 下找不到 UnrealEditor-Cmd.exe' }
$commonArgs = @('-Unattended', '-NullRHI', '-NoSound', '-NoSplash', '-stdout', '-FullStdOutLogOutput')

# 发现测试；Quit 属于同一个 Automation 命令队列，等待前面的任务完成。
& $ue $project '-ExecCmds=Automation List;Quit' @commonArgs
$listExitCode = $LASTEXITCODE
```

先从列表确认目标名称。仓库中的示例注册名为
`OceanCore.Generation.SettingsDeterminism` 和 `OceanCore.Editor.IslandDebuggerDefaultPreview`；
它们可能随代码变化，不代表已在本次进程发现或通过。

```powershell
# 复用上面的变量。过滤字符串可以换成已确认的完整测试名。
$testFilter = 'OceanCore'
$runId = [guid]::NewGuid().ToString('N')
$report = Join-Path (Split-Path $project -Parent) "Saved/AutomationReports/$runId"
& $ue $project "-ExecCmds=Automation RunTests $testFilter;Quit" @commonArgs "-ReportExportPath=$report"
$testExitCode = $LASTEXITCODE
Write-Output "ExitCode=$testExitCode Report=$report"
```

分号必须留在同一个 `-ExecCmds` 参数中；不要另发立即退出命令截断测试。
每次使用独立报告目录，避免把上次报告当作本次结果。默认日志位于项目 `Saved/Logs/`。
CI 调度层应设超时并保存本次 stdout 和报告；超时属于未完成，不能算通过。

## 成功判据与故障分类

- 列举成功：进程完成发现且日志包含目标测试；只有退出码 0 不够。
- 执行成功：退出码 0、本次报告已生成、实际运行数量大于 0、目标测试全部完成且无失败。
  检查报告中的测试名称、状态和数量；跳过、未运行、零匹配均不算目标通过。
- 没发现测试：依次查过滤名、模块加载、测试编译条件、Editor/Client 上下文，
  不要先扩大成全引擎测试。自动化框架只能列出当前宿主可见的测试。
- 宿主在模块/DDC 初始化时终止：是启动失败，不是断言失败。
  DDC 日志明确报告没有可写节点时再评估 `-DDC-ForceMemoryCache` 或修复缓存权限；
  不把它无条件加入所有命令，也不通过替换工具绕过权限审批。
- 实际测试失败：记录完整测试名、首个失败断言、日志和报告目录，再转故障定位技能。
- 涉及复制：单进程 Automation 结果不能替代 Dedicated Server + 至少两个客户端的验证。

## 交付

报告过滤条件、引擎版本、执行数量、通过/失败/跳过数量、退出码和本次报告位置。
只检查了源码或命令时明确写 `STATIC_ONLY`；只有真实宿主执行结果才能声称已验证。
本技能不需要 Python；若增加仓库 Python 辅助脚本，先使用
[python-script-governance](../python-script-governance/SKILL.md)。
