分四步，前两步没有捷径（新增了 C++ 模块和渲染设置）。

## 1. 编译

```
# 1) Launcher 装的（最常见）
(Get-Content 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat' | ConvertFrom-Json).InstallationList |
    Select-Object AppName, InstallLocation

# 2) 注册表里的引擎登记
Get-ChildItem 'HKLM:\SOFTWARE\EpicGames\Unreal Engine' -ErrorAction SilentlyContinue |
    ForEach-Object { [PSCustomObject]@{ Version = $_.PSChildName; Path = (Get-ItemProperty $_.PSPath).InstalledDirectory } }

# 3) 源码编译的自建引擎
Get-ItemProperty 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
```

拿到路径后（记作 <Engine>，就是含 Engine\Build\BatchFiles\ 的那一层）：

```
& '<Engine>\Engine\Build\BatchFiles\Build.bat' LyraEditor Win64 Development '-Project=C:\EpicWkspc\OceanAdventure\LyraTemplate.uproject' -WaitMutex -NoHotReloadFromIDE -NoUBA -MaxParallelActions=1
```

## 2. 开编辑器 → 生成材质

第一次启动会因为 `r.ForwardShading=True` 全量重编着色器，等它跑完。然后**在非 PIE 状态**（没点 Play）打开 Output Log，切 `Cmd` 模式：

```
py "D:/UEPrj/OceanAdventure/Plugins/LineArtCore/Content/Python/CreateLineArtCoreAssets.py"
```

看到 `LINEART_CORE_ASSETS_OK` 才算成功。再跑一次，第二次不应改动任何资产。

产物在内容浏览器 `/LineArtCore/Materials/`（要先开 Settings → **Show Plugin Content**，否则看不见）。

## 3. 把 MPC 填进设置

Project Settings → Game → **Line Art Core** → `Environment Collection` 指向 `MPC_LineArtEnvironment`。不填的话子系统启动时会 warning，材质则退回 MPC 默认值。

## 4. 看效果

新建一个空关卡，拖一个 **Sphere**（用球不用立方体，见下面的坑），然后：

| 要看什么 | 怎么做 |
|---|---|
| 平涂填充 | 球的 Material 槽设成 `M_LineArt_Fill` |
| **墨线轮廓** | 给同一个 Actor **再加一个 StaticMeshComponent**，同一个网格，材质设 `M_LineArt_Outline` |
| 墨色/昼夜 | 选中 `MPC_LineArtEnvironment` 双击，直接改 `InkColor`、`Daylight`——编辑器视口实时响应 |
| 线宽 | 改 MPC 的 `OutlineThickness`，推远相机确认线宽不变（这是屏幕空间恒定那条） |
| 篝火 | 往 Actor 上加 `LineArtPointLight` 组件，**Play 后**才会亮 |

---

## 三个会让你以为"没生效"的点

**① 编辑器视口里子系统不 tick。** `UWorldSubsystem::DoesSupportWorldType` 默认只认 Game 和 PIE。所以不点 Play 时你看到的是 **MPC 的默认值**，昼夜换墨、篝火闪烁、云影飘移全都不动——手动改 MPC 的值倒是实时生效。想在视口里就看到，我可以加一行 `DoesSupportWorldType` 覆盖把 Editor 世界也放进来。

**② 用立方体会看到轮廓裂开。** 这不是 bug，正是文档里反复提的那个坑：硬边法线沿法线挤出，每条折边都会开口。UE 自带的 Sphere 是平滑法线所以正常。立方体要正常必须在 Blender 侧把平均法线烘进顶点色/UV2——那是第 2 步的事。

**③ 只加材质不加第二个组件，就只有填充没有线。** 反转外壳是一个独立的 draw，不是填充材质的一个开关。

---

这一套手动摆组件挺烦的。要我加一个 `ALineArtPreviewActor`（自带 fill + hull 两个组件和一盏点光源，拖进关卡就能看全套）吗？十几行 C++，顺便也是第 2 步做 HISM 时的模板。