---
name: ue5-local-directory-discovery
description: 在 Windows 上搜索当前 Unreal 项目目录及匹配的引擎安装目录。涉及 UE_ROOT 未设置、查找 UE 安装、定位当前 uproject、为编译或测试准备目录时使用；通过 Launcher 清单、注册表与项目 EngineAssociation 验证候选，不硬编码机器路径。不负责执行编译测试、安装引擎、修改项目关联或 Blender 文本块路径兜底。
---

# 本机引擎与项目目录发现

## 契约

只读发现并返回项目文件、项目根、引擎根、实际版本及来源。探测所得绝对路径仅作本次运行参数，
不得写入仓库文件或设置永久环境变量。没有 UE_ROOT 时先搜索，不直接要求用户提供目录。
引擎注册表与 Launcher 只提供引擎候选，不能用它们猜项目检出位置。

## 1. 定位当前项目

- 显式指定的项目优先；否则交互式会话从当前目录运行 `git rev-parse --show-toplevel`，检查退出码。
- 在所得仓库根枚举 `*.uproject`，唯一时选中；多个时结合用户指定的项目确认，不能默认第一项。
- 非 Git 工作区，从当前目录逐级向上寻找最近一层的 `*.uproject`；没有候选才询问项目位置，不扫描全盘。
- 磁盘脚本必须从自身位置推导仓库根（PowerShell 使用 `$PSScriptRoot`），不能借用碰巧正确的工作目录。
  Python 的 `__file__` 不存在时按项目规范停止，不用当前目录兜底。
- 读取选中项目 JSON 的 `EngineAssociation`。下文 `$project` 是已确认的项目文件路径，
  `$projectRoot = Split-Path -Parent $project`，不固定项目文件名。

## 2. 获取引擎候选

优先验证显式配置的 `UE_ROOT`。显式配置失效或版本不匹配时报告，不悄悄换成其它引擎。
未配置时依次读取以下三个来源，合并并按规范化目录去重，保留每个来源及登记标识：

```powershell
# Launcher：ProgramData 从操作系统环境获取，不固定盘符。
if ($env:ProgramData) {
    $manifest = Join-Path $env:ProgramData 'Epic/UnrealEngineLauncher/LauncherInstalled.dat'
    if (Test-Path -LiteralPath $manifest -PathType Leaf) {
        (Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).InstallationList |
            Where-Object { $_.AppName -match '^UE_' } |
            Select-Object AppName, InstallLocation
    }
}

# Launcher / 系统级登记。
Get-ChildItem 'HKLM:\SOFTWARE\EpicGames\Unreal Engine' -ErrorAction SilentlyContinue |
    ForEach-Object {
        [PSCustomObject]@{
            Version = $_.PSChildName
            Path = (Get-ItemProperty -LiteralPath $_.PSPath).InstalledDirectory
        }
    }

# 源码引擎：属性名是关联标识，值是安装目录；排除 PowerShell 元数据。
$builds = Get-ItemProperty 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
if ($builds) {
    $builds.PSObject.Properties |
        Where-Object { $_.Name -notmatch '^PS' -and $_.Value -is [string] } |
        Select-Object @{n='Association';e={$_.Name}}, @{n='Path';e={$_.Value}}
}
```

清单缺失或注册表键不存在时继续其它来源；格式损坏、权限拒绝要记录，不能声称该来源没有安装。
环境配置只查当前进程时若为空，也检查用户级、系统级 `UE_ROOT`，避免漏掉会话创建后新增的设置。

## 3. 匹配与验证

1. 排除空路径、已卸载目录和 Launcher 的非引擎条目（例如 Bridge）。
2. 检查 `Engine/Build/Build.version` 与 `Engine/Binaries/Win64/UnrealEditor-Cmd.exe` 存在，
   读取实际 MajorVersion、MinorVersion、PatchVersion；需要编译时另检查 `Engine/Build/BatchFiles/Build.bat`。
3. 数字型 EngineAssociation 按主次版本匹配（若指定补丁也核对补丁）；GUID/自定义标识按 HKCU
   登记键精确匹配，不仅凭版本号替代源码构建。关联为空时不得自动挑最高版本。
4. 唯一匹配才选择；多个有效匹配或关联为空且未指定引擎时展示来源与版本，请用户选择。
   无匹配时汇报已查询来源，再询问位置；不猜默认安装盘符，不修改 EngineAssociation。
5. 在当前 PowerShell 进程赋值 `$env:UE_ROOT = $engineRoot`，供后续命令使用。
   新工具调用可能启动新进程，必须重新发现或显式传递本次探测结果，不假定环境变量跨调用保留。

## 验证与交付

输出所选项目、关联标识、引擎版本、来源和关键文件检查结果。没有唯一匹配不能输出成功。
目录发现成功不等于构建或测试通过；后续任务回到调用方流程。
本机已只读验证 Launcher 与 HKLM 能发现项目所关联的 5.7；HKCU 无登记，源码引擎分支仅静态复核。
此技能不启动引擎、不提交代码，也不修改注册表、永久环境配置或资产。
