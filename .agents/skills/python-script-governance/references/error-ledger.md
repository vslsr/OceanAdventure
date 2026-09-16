# Python 失败档案

本文件是项目 Python 脚本的追加式失败记录。开始任何 Python 创建、修改、审计、排查或执行任务前必须完整阅读。新失败必须在下一次脚本修改前记录；相同签名复发时更新原条目，不创建重复条目。

## 索引

| ID | 首次发生 | 领域 | 错误签名 | 状态 | 次数 |
| --- | --- | --- | --- | --- | ---: |
| PY-UE-001 | 2026-08-27 | Unreal Python / SceneComponent | `set_relative_rotation() required argument 'sweep'` | VERIFIED | 2 |
| PY-UE-002 | 2026-08-27 | Unreal Editor / 执行入口 | Cmd 模式吃掉 Python 输入（`exec(open(...))`／裸 `import`） | OPEN（第二次） | 2 |
| PY-UE-003 | 2026-08-27 | Unreal Python / GameFeatureData | `Failed to find property 'input_mappings'` | STATIC_ONLY | 1 |
| PY-UE-004 | 2026-08-28 | Unreal Editor-Cmd / DDC 启动 | `no writable nodes available` | VERIFIED | 1 |
| PY-UE-005 | 2026-08-28 | Unreal Python / 资产布局迁移 | `FindAssetPackageReferencers failed` / `AsyncLoading2.cpp !bHasFailed` | VERIFIED | 2 |
| PY-UE-006 | 2026-08-28 | Unreal Commandlet / Interchange 导入 | `SlateApplication CurrentApplication.IsValid()` | VERIFIED | 1 |
| PY-UE-007 | 2026-08-28 | Unreal Python / StaticMesh 材质读回 | `'StaticMesh' object has no attribute 'get_static_materials'` | VERIFIED | 1 |
| PY-UE-008 | 2026-08-28 | Unreal Python / PIE 资产编辑 | `The Editor is currently in a play mode` / 误报资产缺失或创建失败 | VERIFIED | 3 |
| PY-UE-009 | 2026-08-28 | Unreal Python / 读回探针 | `'NoneType' object has no attribute 'get_editor_property'` | VERIFIED | 1 |
| PY-BLENDER-001 | 2026-09-15 | Blender bpy / Pose 骨骼空间 | `String midpoint moved -0.0000m at full draw` | VERIFIED | 1 |
| PY-BLENDER-002 | 2026-09-16 | Blender bpy / Action 通道 API | `'Action' object has no attribute 'fcurves'` | VERIFIED | 1 |
| PY-BLENDER-003 | 2026-09-16 | Blender bpy / 脚本输出可见性 | 宿主报告「run script 后什么都没有」 | VERIFIED | 1 |
| PY-LYRA-001 | 历史记录 | Lyra Python / USTRUCT | `call() takes at most 0 arguments` | VERIFIED | 1+ |
| PY-LYRA-002 | 历史记录 | Lyra Python / EditDefaultsOnly | `cannot be edited on instances` | VERIFIED | 1+ |
| PY-LYRA-003 | 历史记录 | Lyra Python / GameplayTag | `InputConfig did not retain ...` 误报 | VERIFIED | 1+ |
| PY-LYRA-004 | 历史记录 | Lyra Python / 数组幂等 | 重跑后 InputAction 条目倍增 | VERIFIED | 1+ |

## PY-UE-001：SceneComponent 旋转缺少完整位置参数

- 日期：2026-08-27；发生两次。
- 宿主与入口：UE 5.7 Unreal Editor Python。
- 脚本：`Plugins/GameFeatures/Raft/Content/Python/CreateRaftNavalAssets.py`。
- 原始错误：

  ```text
  TypeError: set_relative_rotation() required argument 'sweep' (pos 2) not found
  ```

- 首次错误转换：`configure_helm_blueprint()` 调用 `wheel_component.set_relative_rotation(...)`。
- 根因：按单参数便利接口猜测 UE Python 暴露；UE 5.7 暴露完整 SceneComponent 签名，`rotation`、`sweep`、`teleport` 都是位置参数。
- 预防规则：调用 UE Python 组件变换 API 前检查当前仓库成功调用或反射签名；该调用固定为 `set_relative_rotation(unreal.Rotator(...), False, True)`。
- 修复：补齐两个布尔位置参数，并在调用点记录 UE 5.7 签名原因。
- 验证：用户随后确认脚本运行成功；当前脚本保留三参数调用。
- 状态：`VERIFIED`。
- 复发说明：第一次修补没有真正改变实参，只增加了错误注释/错误位置判断，导致相同错误第二次出现。今后注释不能代替调用点读回检查。

## PY-UE-002：在 Cmd 模式执行裸 Python 表达式

- 日期：2026-08-27；发生一次。
- 宿主与入口：Unreal Editor Output Log，`Cmd` 模式。
- 脚本：`Plugins/GameFeatures/TopDownFeature/Content/Python/create_top_down_assets.py`。
- 原始错误：

  ```text
  尝试执行已废弃的命令：exec(open(r"C:/EpicWkspc/OceanAdventure/Plugins/GameFeatures/TopDownFeature/Content/Python/create_top_down_assets.py", encoding="utf-8").read())
  ```

- 首次错误转换：命令没有进入 Python 解释器，`exec` 被 UE 控制台当作引擎 Console Command。
- 根因：混淆 Output Log 的 `Cmd` 与 Python 输入模式。
- 预防规则：`Cmd` 模式执行文件只使用 `py "<script>"`；或用编辑器的 Execute Python Script 文件入口。
- 修复：改用 `py "<script>"`（当时写的是绝对路径；路径本身已按仓库的绝对路径禁令整改，见下方第二次发生）。
- 验证：下一次运行进入脚本并输出 Python traceback，证明入口已正确切换到 Python。
- 状态：`VERIFIED`。
- 发生次数：2。

### 第二次发生：2026-09-16，入口改成 `import` 之后

- 宿主与入口：UE 5.7 Unreal Editor Output Log，**`Cmd` 模式**。
- 脚本：`Plugins/LineArtCore/Content/Python/CreateLineArtCoreAssets.py`。
- 现象：**没有任何报错**。Output Log 只回显三行输入，脚本零输出：

  ```text
  Cmd: import importlib, CreateLineArtCoreAssets
  Cmd: importlib.reload(CreateLineArtCoreAssets)
  Cmd: CreateLineArtCoreAssets.main()
  ```

- 首次错误转换：三行都被当作引擎 Console Command 丢弃，从未进入 Python 解释器。
- 复发原因：**上一条预防规则只覆盖了 `py "<file>"` 这一种入口。** 为落实绝对路径禁令，四个脚本的
  入口改成了「模块 `import` + `reload` + `main()`」——这种写法必须在 **Python 输入模式**执行，
  而规则里一个字都没提模式本身，只规定了 Cmd 模式下该怎么写文件路径。规则跟着旧入口走，入口一换就失效。
- **比第一次更难发现**：`exec(open(...))` 至少会触发「尝试执行已废弃的命令」，而裸 `import` 在 Cmd 模式下
  被静默丢弃。看上去像「脚本跑了但什么都没做」，不像「脚本没跑」。
- 预防规则（替代上一条，按模式而不是按入口写）：
  1. **先确认模式，再谈写法**。Output Log 输入框左侧的下拉决定一切：`Cmd` 只吃引擎控制台命令，
     Python 输入模式才吃 Python。
  2. Python 入口一律在 **Python 输入模式**执行，包括 `import` / `reload` / `main()` 三行式。
  3. 只有在 `Cmd` 模式里才用 `py "<script>"`，且这是唯一允许在 Cmd 模式出现的 Python 入口。
  4. **每个资产脚本必须在结尾打印稳定的成功标记**（如 `LINEART_CORE_ASSETS_OK`），
     并在文档里写明「没有这一行就等于没成功」。静默不执行只能靠标记缺失来判定，没有别的信号。
- 修复：四个脚本的 docstring 补上「Python 输入模式，不是 Cmd 模式」的显式说明与模式切换位置。
- 状态：`OPEN`。待用户在 Python 输入模式重跑并出现 `LINEART_CORE_ASSETS_OK` 后转 `VERIFIED`。

## PY-UE-003：GameFeatureAction 基类包装器读取派生属性失败

- 日期：2026-08-27；发生一次。
- 宿主与入口：UE 5.7 Unreal Editor，`Cmd: py ".../create_top_down_assets.py"`。
- 脚本：`Plugins/GameFeatures/TopDownFeature/Content/Python/create_top_down_assets.py`。
- 原始错误：

  ```text
  File ".../create_top_down_assets.py", line 80, in is_legacy_input_action
    mappings = action.get_editor_property("input_mappings")
  Exception: GameFeatureAction: Failed to find property 'input_mappings' for attribute 'input_mappings' on 'GameFeatureAction_AddInputContextMapping'
  ```

- 伴随噪声：`Asset has been saved with empty engine version` 是旧资产兼容性警告，不是本次 traceback 根因。
- 首次错误转换：遍历 `UGameFeatureData::Actions` 后，脚本试图通过数组返回的 `GameFeatureAction` 基类包装器读取派生类的 `InputMappings`。
- 根因：`action.get_class()` 能报告 `GameFeatureAction_AddInputContextMapping`，但不代表 UE 5.7 的基类 Python 包装器暴露派生属性。
- 预防规则：如果架构上整个 Action 类型都禁止存在，使用 `action.get_class().get_name()` 作为迁移键，不读取派生字段；如果必须检查字段值，先验证 typed wrapper，失败则使用所属 GameFeature 的窄原生编辑器桥接。
- 修复：TopDownFeature 按具体 Action 类清理所有 Add Input Mapping / Add Input Binding；不再读取 `input_mappings` 或 `input_configs`。
- 验证：Python AST 与 `git diff --check` 已通过；尚待 Unreal Editor 连续运行两次并出现 `TOPDOWN_ASSETS_MIGRATED`。
- 状态：`STATIC_ONLY`。

## PY-UE-004：受限环境没有可写 DDC 节点

- 日期：2026-08-28；发生三次。
- 宿主与入口：UE 5.7.4 `UnrealEditor-Cmd.exe`，`-run=pythonscript` 执行
  `Plugins/NavalCore/Content/Python/MigrateNavalCoreContentLayout.py`。
- 原始错误：

  ```text
  LogWindows: Error: appError called: Fatal error: [File:D:\build\++UE5\Sync\Engine\Source\Developer\DerivedDataCache\Private\DerivedDataBackends.cpp] [Line: 208]
  Unable to use default cache graph 'InstalledDerivedDataBackendGraph' because there are no writable nodes available.Add -DDC-ForceMemoryCache to the command line to bypass this if you need access to the editor settings to fix the cache configuration.
  ```

- 首次错误转换：引擎初始化 DDC 时终止，尚未进入 PythonScript commandlet，也没有执行迁移脚本。
- 根因：受限执行环境不能更新用户级 Zen 安装，也不能写
  `C:/Users/db/AppData/Local/UnrealEngine/Common/DerivedDataCache`，默认 DDC 图因此没有可写节点。
- 预防规则：在受限环境启动 UE commandlet 时显式传入 `-DDC-ForceMemoryCache`；若宿主仍需要用户级目录，申请在沙箱外执行，不把启动失败误判为 Python 脚本失败。
- 修复：使用 `-DDC-ForceMemoryCache`，并在沙箱外重新执行同一个 commandlet；迁移脚本本身未修改。
- 验证证据：2026-08-28 重跑成功越过 DDC，日志出现
  `LogInit: Executing Class /Script/PythonScriptPlugin.PythonScriptCommandlet` 和
  `LogPythonScriptCommandlet: Display: Running Python script`。
- 状态：`VERIFIED`。
- 发生次数：1。

## PY-UE-005：Blueprint/Redirector 迁移状态处理不安全

- 日期：2026-08-28；发生两次。
- 宿主与入口：UE 5.7.4 `UnrealEditor-Cmd.exe`，`-run=pythonscript` 执行
  `Plugins/NavalCore/Content/Python/MigrateNavalCoreContentLayout.py`。
- 原始错误：

  ```text
  LogEditorAssetSubsystem: Error: FindAssetPackageReferencers failed: Could not load asset. The asset '/NavalCore/Naval/BP_Naval_CannonballProjectile.BP_Naval_CannonballProjectile' exists but was not able to be loaded.
  LogEditorAssetSubsystem: Error: DeleteAsset failed: Could not find the source asset. The asset '/NavalCore/Naval/BP_Naval_CannonballProjectile.BP_Naval_CannonballProjectile' exists but was not able to be loaded.
  LogInit: Display: Failure - 12 error(s), 52 warning(s)
  ```

- 第二次原始错误：

  ```text
  LogLinker: Warning: [AssetLog] C:\EpicWkspc\OceanAdventure\Plugins\NavalCore\Content\Blueprints\Cannon\BP_Naval_Cannon.uasset: Error opening file.
  Script Stack (1 frames) :
  /Script/EditorScriptingUtilities.EditorAssetLibrary.RenameAsset
  LogWindows: Error: appError called: Assertion failed: !bHasFailed [File:D:\build\++UE5\Sync\Engine\Source\Runtime\CoreUObject\Private\Serialization\AsyncLoading2.cpp] [Line: 1426]
  ```

- 首次错误转换：`remove_unreferenced_redirectors()` 调用
  `find_package_referencers_for_asset()`；在此之前，同一 Blueprint 包已先保存到
  `/NavalCore/Arts/Cannon`，又在 `/NavalCore/Blueprints/Cannon` 留下 Redirector。
- 根因：Asset Registry 会为 Blueprint 包返回 Blueprint、GeneratedClass 等多条
  `AssetData`。脚本按单条 `asset_class_path` 分类且没有按 `package_name` 去重，使同一包进入
  Arts 和 Blueprints 两个计划；`exact_asset()` 又只比较包路径，没有排除
  `ObjectRedirector`，让 Redirector 通过了目标验证。最后脚本对不可加载的旧 Redirector
  调用 EditorAssetSubsystem 查询/删除，产生宿主错误。
- 第二次复发原因：首次修复已按包去重并排除 Redirector，但在同一个编辑器进程里删除目标路径的
  Redirector 后立刻把真实 Blueprint 重命名到相同包路径。UE 5.7 的异步加载器仍持有刚删除包的
  失败/卸载状态；Projectile 恰好完成，随后 Cannon 在 `RenameAsset` 复用同一路径时触发
  `AsyncLoading2.cpp` 断言并使宿主崩溃。修复没有把“释放冲突路径”和“复用路径”隔离到两次宿主会话。
- 预防规则：资产迁移计划必须以 `package_name` 为稳定键去重；`BP_` 包名或任一记录为
  Blueprint 时整包按 Blueprint 分类；所有“真实资产”检查必须显式排除
  `ObjectRedirector`；未知或不可加载 Redirector 只报告，不用 `delete_asset` 强删。
- 补充预防规则：如果目标包被 Redirector 占用，本次宿主会话只释放冲突 Redirector 并返回
  “需要重启/重跑”的明确状态；不得在同一进程中立即把真实资产重命名到刚释放的包路径。
- 修复：按包去重、识别当前半迁移状态并停止自动清理普通旧 Redirector；目标 Redirector 冲突
  改为跨宿主会话的两阶段流程。恢复运行已将 `/NavalCore/Arts/Cannon/BP_*` 移至
  `/NavalCore/Blueprints/Cannon`，后续无变更运行不再强制重存目标资产。
- 验证证据：2026-08-28 使用 UE 5.7.4 commandlet 完成恢复迁移后，又连续运行当前脚本两次；
  两次均输出 `NAVALCORE_LAYOUT_MIGRATION_OK_WITH_REDIRECTORS`、
  `Python script executed successfully` 与 `Success - 0 error(s)`。Blueprints/Arts 下 18 个
  `.uasset` 的 SHA-256 在两次运行前后完全一致；真实 `BP_Naval_Cannon`（25944 bytes）与
  `BP_Naval_CannonballProjectile`（24389 bytes）位于 `/NavalCore/Blueprints/Cannon`，Arts
  下无真实 Blueprint。旧路径仅剩三个报告出的 Redirector，脚本未强删。
- 状态：`VERIFIED`。
- 发生次数：2。

## PY-UE-006：Commandlet 的 Interchange 导入触发 Slate 断言

- 日期：2026-08-28；发生一次。
- 宿主与入口：UE 5.7.4 `UnrealEditor-Cmd.exe`，`-run=pythonscript` 执行
  `Plugins/GameFeatures/Raft/Content/Python/CreateRaftNavalAssets.py`，带
  `-DDC-ForceMemoryCache -unattended -stdout -FullStdOutLogOutput`。
- 原始错误：

  ```text
  LogOutputDevice: Warning:
  Script Stack (1 frames) :
  /Script/AssetTools.AssetTools.ImportAssetTasks

  LogWindows: Error: appError called: Assertion failed: CurrentApplication.IsValid()
  [File:D:\build\++UE5\Sync\Engine\Source\Runtime\Slate\Public\Framework\Application\SlateApplication.h]
  [Line: 321]
  ```

- 首次错误转换：`import_life_raft_mesh()` 调用 `AssetTools.import_asset_tasks()`；日志先报告
  `Interchange import completed` 并保存 `/Raft/Vehicles/LifeRaft/SM_LifeRaft`，随后
  ContentBrowser/AssetTools 调用链访问不存在的 Slate Application 并使宿主崩溃。
- 根因：UE 5.7 Interchange 的该批量导入完成路径包含编辑器 UI/Content Browser 通知；
  `UnrealEditor-Cmd` 的 PythonScript commandlet 没有有效 Slate Application。脚本此前只在完整
  Editor 中使用，未区分“源 FBX 已存在且目标 Mesh 已存在”与“必须重新导入”的宿主能力。
- 预防规则：Commandlet 资产脚本不得无条件重导入已有 FBX。先用稳定资产路径加载并读回目标；
  已存在时复用，不进入 Interchange。确需首次导入时使用完整 Unreal Editor 宿主，或经真实验证的
  commandlet-safe 导入入口；发生崩溃后必须把本次视为部分写入并重启宿主再验证。
- 修复：`import_life_raft_mesh()` 先加载稳定目标路径；目标存在时校验其类型为 `StaticMesh` 并直接
  复用。目标缺失且宿主是 `-run=pythonscript` commandlet 时，在进入 Interchange 前抛出带完整
  Editor 操作指引的 `RuntimeError`；仅完整 Editor 宿主保留首次 FBX 导入路径。
- 验证证据：修复后在 UE 5.7.4 `UnrealEditor-Cmd.exe` 中多次重跑同一脚本，均输出
  `Python script executed successfully`；使用 `-ddc=InstalledNoZenLocalFallback` 的最终两次资产
  宿主验证分别覆盖 OceanAdventure 与 Raft，均为退出码 0、`Success - 0 error(s)`。Raft 重跑加载
  并复用 `/Raft/Vehicles/LifeRaft/SM_LifeRaft`，未再次进入 `ImportAssetTasks`，稳定命名的
  GameFeature Action 与资产配置未出现重复增长。
- 状态：`VERIFIED`。
- 发生次数：1。

## PY-UE-007：StaticMesh 包装器未暴露 get_static_materials

- 日期：2026-08-28；发生一次。
- 宿主与入口：UE 5.7 Unreal Editor，通过 Nwiro MCP `execute_python` 执行只读材质读回探针。
- 脚本：内联只读探针；目标资产
  `/NavalCore/Arts/Cannon/Meshes/SM_Naval_Cannon`。仓库脚本
  `Plugins/NavalCore/Content/Python/RepairNavalCannonMaterials.py` 未在本次失败中执行。
- 原始错误：

  ```text
  Traceback (most recent call last):

    File "<string>", line 3, in <module>

  AttributeError: 'StaticMesh' object has no attribute 'get_static_materials'
  ```

- 首次错误转换：只读探针直接调用 `mesh.get_static_materials()`。
- 根因：UE 5.7 当前 `StaticMesh` Python 包装器没有暴露该便利方法；同一属性可通过
  `mesh.get_editor_property("static_materials")` 读取。仓库修复脚本已经使用
  `getattr(mesh, "get_static_materials", None)` 并在缺失时回退到反射属性，因此材质修复与双跑验证未受影响。
- 预防规则：StaticMesh 材质数组读回必须先探测 `get_static_materials`；不可用时固定读取
  `static_materials`，不得在临时验证探针中省略与正式脚本相同的兼容分支。
- 修复：使用与正式修复脚本相同的 `getattr` 探测与 `static_materials` 反射属性回退，重新运行材质实例 Parent 与六槽读回探针。
- 验证证据：同一 UE 5.7 Editor 宿主重跑成功，六个槽分别读回 Wood、DarkWood、DarkMetal、WheelRim、Bronze、Bore；六个材质实例均解析到
  `/InterchangeAssets/Materials/FBXLegacyPhongSurfaceMaterial` Parent，未再出现 traceback。
- 状态：`VERIFIED`。
- 发生次数：1。

## PY-UE-008：PIE 中 EditorAssetLibrary 拒绝资产查询

- 日期：2026-08-28；发生一次。
- 宿主与入口：UE 5.7 Unreal Editor Output Log，Cmd 模式执行
  `py "C:/EpicWkspc/OceanAdventure/Plugins/NavalCore/Content/Python/RepairNavalCannonMaterials.py"`。
- 脚本：
  - `Plugins/NavalCore/Content/Python/RepairNavalCannonMaterials.py`；
  - `Plugins/GameFeatures/Raft/Content/Python/CreateRaftNavalAssets.py`；
  - `Plugins/GameFeatures/OceanAdventure/Content/Python/CreateNavalP0Assets.py`。
- 原始错误：

  ```text
  LogUtils: Error: The Editor is currently in a play mode.
  LogPython: Error: Traceback (most recent call last):
  LogPython: Error:   File "C:/EpicWkspc/OceanAdventure/Plugins/NavalCore/Content/Python/RepairNavalCannonMaterials.py", line 171, in <module>
  LogPython: Error:     main()
  LogPython: Error:   File "C:/EpicWkspc/OceanAdventure/Plugins/NavalCore/Content/Python/RepairNavalCannonMaterials.py", line 133, in main
  LogPython: Error:     mesh = load_asset(MESH_PATH)
  LogPython: Error:   File "C:/EpicWkspc/OceanAdventure/Plugins/NavalCore/Content/Python/RepairNavalCannonMaterials.py", line 46, in load_asset
  LogPython: Error:     require(
  LogPython: Error:   File "C:/EpicWkspc/OceanAdventure/Plugins/NavalCore/Content/Python/RepairNavalCannonMaterials.py", line 35, in require
  LogPython: Error:     raise RuntimeError(message)
  LogPython: Error: RuntimeError: Required asset does not exist: /NavalCore/Arts/Cannon/Meshes/SM_Naval_Cannon
  ```

  同一 PIE 会话随后复发：

  ```text
  LogUtils: Error: The Editor is currently in a play mode.
  RuntimeError: Missing /Raft/Build/Materials/M_Raft_BuildPreview_Invalid; run CreateRaftBuildPieceAssets.py first
  ```

  ```text
  LogUtils: Error: The Editor is currently in a play mode.
  InputMappingContext /OceanAdventure/Input/IMC_OceanNaval.IMC_OceanNaval正在使用中。
  External referencers:
    LyraPlayerInput /OceanAdventure/Maps/UEDPIE_0_L_NavalP0...LyraPlayerInput_0
  RuntimeError: Unable to create /OceanAdventure/Input/IMC_OceanNaval
  ```

- 首次错误转换：`load_asset()` 在 PIE 中调用
  `EditorAssetLibrary.does_asset_exist()`；宿主先拒绝编辑器资产操作，函数随后返回假值，脚本把宿主限制误报为资产不存在。
- 根因：这些脚本只能在非 Play 的完整 Editor 会话中修改并保存资产，但没有在首次
  `EditorAssetLibrary` 调用前检测 PIE 状态，也没有把执行前提写成可执行门禁。PIE 中查询返回假值后，
  Naval 脚本还进入“创建缺失资产”分支，碰到 `LyraPlayerInput` 对正在使用的 IMC 的外部引用。
- 伴随噪声：`AutomationController` 的 large delta 与 `PSOHitching` 计数是 PIE 运行噪声，不是 Python 根因。
- 复发原因：首次错误发生后尚未来得及把门禁写入脚本，用户在同一 PIE 会话继续执行了另外两个资产脚本；原有的文档性执行说明不能阻断错误宿主。
- 预防规则：所有通过 `EditorAssetLibrary` 修改内容资产的完整 Editor 脚本，应在首次资产查询前调用 UE 5.7 已验证的
  `unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()` fail-fast；提示必须明确要求点击 Stop 后重跑，不能继续并误报目标资产缺失或进入创建分支。
- 修复：已在真实 UE 5.7 Editor 反射中确认 `LevelEditorSubsystem.is_in_play_in_editor() -> bool`，并在三个入口的
  `main()` 首次 Asset Registry / EditorAssetLibrary 调用前增加统一门禁。门禁在 PIE 时明确要求点击 Stop 后重跑。
- 验证证据：修复前的真实 Editor 探针读回 `IS_IN_PLAY_IN_EDITOR=True`，证明所选 API 能识别错误宿主状态。随后重新启动完整 UE 5.7 Editor，读回
  `IS_IN_PLAY_IN_EDITOR=False`，并确认 `IMC_OceanNaval`、`M_Raft_BuildPreview_Invalid`、`SM_Naval_Cannon` 均存在；三个修复后脚本分别连续执行两次成功，无 traceback。最终读回确认 Raft Catalog 为 10 条且无重复、Naval Fire 不消费输入、炮网格六个材质槽全部正确。
- 状态：`VERIFIED`。
- 发生次数：3。

## PY-UE-009：读回探针的 require helper 漏返回值

- 日期：2026-08-28；发生一次。
- 宿主与入口：UE 5.7 Unreal Editor，通过 Nwiro MCP `execute_python` 执行内联只读汇总探针。
- 脚本：内联只读探针；仓库中的三个资产脚本均已在此前连续运行两次成功，本次没有执行或修改它们。
- 原始错误：

  ```text
  Traceback (most recent call last):

    File "<string>", line 8, in <module>

  AttributeError: 'NoneType' object has no attribute 'get_editor_property'
  ```

- 首次错误转换：探针执行
  `catalog = require(EditorAssetLibrary.load_asset(...), ...)` 后读取
  `catalog.get_editor_property("pieces")`。
- 根因：内联探针的 `require(value, message)` 只在假值时抛错，却漏写成功路径的
  `return value`；Catalog 实际加载成功，但 helper 把局部变量变成了 `None`。
- 预防规则：内联读回探针复用项目既有 helper 语义时，`require` 的成功路径必须显式返回输入值；在第一个资产字段读取前先打印或断言包装类型。
- 修复：补回内联 helper 的 `return value`，并在首次字段访问前断言 Catalog 包装类型为
  `BuildPieceCatalog`。
- 验证证据：同一 UE 5.7 Editor 宿主重新执行纯读回探针成功，输出
  `READBACK_OK IS_IN_PLAY=False`、`READBACK_RAFT_CATALOG_COUNT=10 DUPLICATES=0`、
  `READBACK_NAVAL_FIRE_CONSUME=False` 和六个正确炮材质槽映射；无 traceback。
- 状态：`VERIFIED`。
- 发生次数：1。

## PY-BLENDER-001：Pose 骨骼的 location/rotation 用的是骨骼局部空间，不是世界轴

- 日期：2026-09-15；发生一次。
- 宿主与入口：Blender（用户在 Scripting 工作区运行磁盘脚本）。
- 脚本：`blender/script/python/create_wood_bow.py`。
- 原始错误：

  ```text
  Python: Traceback (most recent call last):
    File "\create_wood_bow.py", line 744, in <module>
    File "\create_wood_bow.py", line 727, in build_wood_bow
    File "\create_wood_bow.py", line 647, in validate_draw_pose
  RuntimeError: String midpoint moved -0.0000m at full draw, expected 0.1800m. The string_mid weights are not reaching it.
  ```

- 首次错误转换：`validate_draw_pose()` 执行
  `rig.pose.bones["string_mid"].location = (0.0, 0.0, BOW_STRING_PULL)`，随后读回形变网格，
  弦中点在世界 Y 上的位移为 0。
- 根因：`PoseBone.location` 与 `PoseBone.rotation_euler` 都定义在**骨骼局部空间**，其中局部 Y
  沿 head→tail，局部 X/Z 由 roll 决定。脚本按世界轴写了分量（把「沿 +Y 拉弦」写成局部 Z），
  位移落到了与世界 Y 垂直的方向上，世界 Y 分量因此恰好为 0。断言本身正确，被测的驱动代码错了。
  **不是蒙皮权重问题**：同一次运行里 `validate_bow()` 的权重归一化断言已全部通过。
- 预防规则：pose 驱动一律不直接写世界轴分量。先把世界方向换算到该骨骼的局部空间
  （`bone.matrix_local.to_3x3().inverted() @ world_vector`），再写 `location`；旋转同样用
  换算后的局部轴构造 `Quaternion(local_axis, angle)`，不要靠猜 roll 让局部 X 恰好等于世界 X。
  失败信息里必须带上三维位移全量，否则「没动」和「动错方向」两种故障长得一模一样。
- 修复：`validate_draw_pose()` 改为按 `matrix_local` 换算世界 +Y 平移与世界 X 旋转；断言改为
  同时报告世界 Y 位移与三维位移模长。
- 验证证据：2026-09-15 用户在 Blender 5.1.0 中重跑成功，产物 `blender/models/SK_WoodBow.fbx`
  （40732 字节）已提交到 main（`3d51379`，`08b394b` 为再次导出）。该文件能存在即证明
  `validate_draw_pose()` 已通过——它正是修复前抛错的那一步，且排在导出之前。FBX 内含且仅含
  契约里的七根骨 `root/grip/limb_upper/limb_lower/string_upper/string_mid/string_lower`，
  无 `nock`，并带 Deformer 与两个材质，说明骨架、蒙皮与导出参数一并成立。
- 状态：`VERIFIED`。
- 发生次数：1。

## PY-BLENDER-002：Blender 5.x 的 Action 没有 `fcurves`，通道挂在 slot 的 channelbag 上

- 日期：2026-09-16；发生一次。
- 宿主与入口：Blender 5.1.0（用户在 Scripting 工作区运行，Text Block 显示为
  `\create_wood_bow.py.002`）。
- 脚本：`blender/script/python/create_wood_bow.py`。
- 原始错误：

  ```text
  Python: Traceback (most recent call last):
    File "\create_wood_bow.py.002", line 1312, in <module>
    File "\create_wood_bow.py.002", line 1256, in build_wood_bow
    File "\create_wood_bow.py.002", line 994, in bake_action
  AttributeError: 'Action' object has no attribute 'fcurves'
  ```

- 首次错误转换：`bake_action()` 在插完关键帧后执行 `if not action.fcurves:` —— 那行本来是用来
  挡「Action 没绑 slot、keyframe_insert 静默写空」的断言，结果它自己先用了一个不存在的属性。
- 根因：4.4 引入 slotted Action 后，`Action.fcurves` 只是 legacy Action 的兼容外壳；5.x 取消
  legacy Action，通道改挂在 `action.layers[].strips[].channelbag(slot).fcurves` 上，属性被移除。
  脚本按 4.x 的形状读通道，且**三处**都这么读：非空断言、设 LINEAR 插值、`validate_clips()` 里
  按 `data_path` 取骨骼名。
- 为什么之前没发现：这三处都排在网格 FBX 导出**之后**。`PY-BLENDER-001` 结案时凭
  `blender/models/SK_WoodBow.fbx` 存在判定通过，那个产物只证明到导出网格为止；
  `blender/models/` 里从来没有出现过 `A_WoodBow_*.fbx`，也就是说 clip 一次都没烘成功过。
  **产物证据只能证明它排在哪一步之前，不能证明整条脚本跑完。**
- 预防规则：
  1. 读 Action 通道一律走兼容访问器（仓库里 `boiler_animation.py` / `door_animation.py` /
     `claude-blender.md` 早就有 `get_fcurves()`，写新脚本前先 grep 一遍），不直接写
     `action.fcurves`：有该属性走 legacy，
     没有就遍历 `layers[].strips[].channelbags[]`（或按当前 slot 取 `channelbag(slot)`）。
  2. 赋 Action 的同时把 slot 绑上（`animation_data.action_slot`），再插关键帧；
     赋值散落在回放、导出等多处时抽成一个 `assign_action()`，不要各写各的。
  3. 结案证据必须落在**脚本最后一步之后**的产物或成功标记上；用中途产物结案等于没验证。
- 修复：新增 `assign_action()` / `bind_action_slot()` / `get_fcurves()` 三个兼容入口
  （`get_fcurves` 沿用 `boiler_animation.py` 等脚本里已有的同名 helper），三处读通道全部改走它；非空断言的报错信息带上走的是哪条 API 与 slot 名。
- 验证证据：普通 CPython 用假 bpy 对象分别模拟 legacy Action（有 `fcurves`）与 slotted
  Action（只有 `layers/strips/channelbags`），两条路径都取到同一组通道，缺通道时按预期抛错。
  2026-09-16 用户在 Blender 5.1.0 重跑：`preview_wood_bow.py` 的报告列出四段 clip，
  每段 11 条曲线（两根弓臂四元数各 4 + 弦中点位移 3），帧跨度 0..288 / 0..24 / 0..192 / 0..15，
  逐段回放实测弦位移 20.0 / 180.0 / 24.0 / 192.2 mm。四段 Action 能被烘出来并被读回，
  就证明 `bake_action()` 里那次 `AttributeError` 已消失、通道读取走通了 slotted API。
- 状态：`VERIFIED`（限于 Blender 侧的通道读取与烘焙。合并包
  `blender/models/A_WoodBow_Clips.fbx` 的导出与 UE 导入仍未验证，属
  `blender-skeletal-asset-workflow` 里标注的未验证项，不在本条范围内）。
- 发生次数：1。

## PY-BLENDER-003：脚本只用 print 报告，等于在宿主里没有输出

- 日期：2026-09-16；发生一次（导致三轮误诊）。
- 宿主与入口：Blender 5.1.0，Text Editor 的 Run Script（信息面板记录 `bpy.ops.text.run_script()`）。
- 脚本：`blender/script/python/create_wood_bow.py`、`preview_wood_bow.py`。
- 原始现象：用户连续报告「什么都没有」「run script 后什么都没有」；信息面板只有算子记录，
  没有任何脚本输出，也没有 traceback。
- 根因：`print()` 只写到**系统控制台**，Windows 上默认隐藏（窗口 → 切换系统控制台）。
  Blender 的 Python 控制台和信息编辑器都不显示它。脚本把全部校验结论、clip 清单、
  导出路径都放在 print 里，于是从 UI 看，一次成功的运行和一次什么都没做的运行完全一样。
- 误诊代价：我据此三次让用户「看控制台输出」，每次都得到「什么都没有」，把注意力引到
  「clip 是不是没烘出来」上；期间真正的两个 bug（slot 读错、幅度太小到看不见）
  是靠读代码发现的，不是靠这条现象。
- 预防规则：
  1. 面向宿主 UI 的脚本，结论必须落在**用户不用找就能看到的地方**：写进 Text datablock
     （Text Editor 里可打开）并尝试 `window_manager.popup_menu`，print 只作为补充；
  2. 报告每次运行**覆盖**同名 datablock，不要追加，否则第二次运行的结论被上一次淹没；
  3. 没有 window manager（background 模式）时 popup 必须吞掉异常，datablock 照写；
  4. 指导用户排查前，先确认他看得到脚本的输出通道；「看控制台」不是通用建议，
     Windows 上要先让他开系统控制台。
- 修复：两个脚本新增 `report(lines)`，同时写 `WoodBow_Report` datablock、弹窗与 print；
  收尾结论全部改走它。
- 验证证据：普通 CPython 用假 bpy 验证——datablock 每次运行被替换而非追加、
  popup 被调用、`window_manager` 为 None 时不抛错且 datablock 仍写入。
  2026-09-16 用户在 Blender 5.1.0 重跑后，首次看到脚本输出：`WoodBow_Report` 面板里
  完整列出四段 clip 与实测位移。同一份信息在此前三轮里一直存在于 print，用户一次也没看到。
- 状态：`VERIFIED`。
- 发生次数：1。

## PY-LYRA-001：USTRUCT 包装器拒绝带参数构造

- 日期：历史记录；至少一次。
- 原始错误：`call() takes at most 0 arguments`。
- 根因：并非所有 Unreal Python USTRUCT 包装器都支持位置或关键字构造。
- 预防规则：先验证包装器；`FLyraAbilitySet_GameplayAbility` 走现有原生编辑器桥接，不重复尝试构造。
- 技术细节与验证：见 `lyra-editor-asset-automation`。
- 状态：`VERIFIED`。

## PY-LYRA-002：EditDefaultsOnly 字段不能在结构实例上修改

- 日期：历史记录；至少一次。
- 原始错误：`Property ... cannot be edited on instances`。
- 根因：对 Python 结构实例调用 `set_editor_property` 修改 `EditDefaultsOnly` 字段。
- 预防规则：支持关键字构造的 `LyraInputAction` 用关键字一次构造；AbilitySet 条目使用既有原生桥接。
- 技术细节与验证：见 `lyra-editor-asset-automation`。
- 状态：`VERIFIED`。

## PY-LYRA-003：GameplayTag 包装器身份比较产生写入误报

- 日期：历史记录；至少一次。
- 原始错误：`RuntimeError: InputConfig did not retain IA_... -> <Struct 'GameplayTag' ... {}>`。
- 根因：UE 5.7 `FGameplayTag` Python 包装器的直接 `==` 可能比较包装器身份，不是底层 Tag 值。
- 预防规则：使用 `GameplayTagLibrary.equal_equal_gameplay_tag`，缺失时比较 `export_text()`；不要读取 `tag_name`。
- 技术细节与验证：见 `lyra-editor-asset-automation`。
- 状态：`VERIFIED`。

## PY-LYRA-004：数组过滤退化为累加器

- 日期：历史记录；至少一次。
- 现象：`DA_InputConfig_OceanAdventure.NativeInputActions` 重跑后增长到 26 条，7 个 TopDown Action 各出现三份。
- 根因：用 `in` / `not in` 比较 UObject/USTRUCT 包装器，旧条目未命中，脚本每次只追加。
- 预防规则：用 GameplayTag 语义比较或稳定资产路径过滤；重建后断言精确长度，并连续运行两次。
- 技术细节与验证：见 `lyra-editor-asset-automation`。
- 状态：`VERIFIED`。
