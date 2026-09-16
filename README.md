# 建造系统

help: `doc/help/BuildSystem.md`

## 编译

**仓库内的路径一律相对**，所以下面的命令在任何检出位置都能用——在仓库根目录执行即可：

```powershell
& "$Engine\Engine\Build\BatchFiles\Build.bat" LyraEditor Win64 Development "-Project=$PWD\LyraTemplate.uproject" -WaitMutex -NoHotReloadFromIDE -NoUBA -MaxParallelActions=1
```

`$Engine` 是本机 UE 5.7 的安装根（含 `Engine\Build\BatchFiles\` 的那一层）。
**引擎装在哪里不是仓库能知道的事**，所以它只能来自本机——三种装法查法不同，挨个试：

```powershell
# 1) Launcher 装的
(Get-Content 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat' | ConvertFrom-Json).InstallationList |
    Select-Object AppName, InstallLocation

# 2) 注册表里的引擎登记
Get-ChildItem 'HKLM:\SOFTWARE\EpicGames\Unreal Engine' -ErrorAction SilentlyContinue |
    ForEach-Object { [PSCustomObject]@{ Version = $_.PSChildName; Path = (Get-ItemProperty $_.PSPath).InstalledDirectory } }

# 3) 源码自建的引擎
Get-ItemProperty 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
```

查到后 `$Engine = '<查出来的路径>'`，再跑上面那条编译命令。

**或者完全绕开它**：双击 `LyraTemplate.uproject`，编辑器发现模块过期会问你要不要重新编译，
点是即可——它按 `.uproject` 里的 `"EngineAssociation": "5.7"` 自己找引擎，不需要你知道路径。

## 路径约定（硬性）

**仓库里的任何文件都不得写死绝对路径**——不写盘符、不写某台机器的检出位置。
换一台机器、换一个检出目录就废掉的路径，是这个仓库反复踩到的坑
（`D:\UEPrj\...`、`C:\EpicWkspc\...`、`E:\...` 都出现过）。

按目标位置分三种写法：

| 目标 | 写法 |
| --- | --- |
| 仓库内的文件 | 从脚本自身位置推导，如 `Path(__file__).resolve().parents[3]` |
| UE 资产 | 用 `/GameFeature/...` 这类虚拟包路径，不用磁盘路径 |
| 编辑器 Python 入口 | 用模块名 `import`，UE 已把插件的 `Content/Python` 放进 `sys.path` |
| 仓库外（引擎安装、参考工程） | **没有相对形式**，只能走环境变量或本机探测，且不给默认值 |

最后一行是重点：给一个机器相关的默认值，只会把「你没告诉我它在哪」变成很久以后
一句莫名其妙的「这个路径不存在」。宁可当场报错说清楚要设哪个环境变量。
