# Python 失败档案

本文件是项目 Python 脚本的追加式失败记录。开始任何 Python 创建、修改、审计、排查或执行任务前必须完整阅读。新失败必须在下一次脚本修改前记录；相同签名复发时更新原条目，不创建重复条目。

## 索引

| ID | 首次发生 | 领域 | 错误签名 | 状态 | 次数 |
| --- | --- | --- | --- | --- | ---: |
| PY-UE-001 | 2026-08-27 | Unreal Python / SceneComponent | `set_relative_rotation() required argument 'sweep'` | VERIFIED | 2 |
| PY-UE-002 | 2026-08-27 | Unreal Editor / 执行入口 | `尝试执行已废弃的命令：exec(open(...))` | VERIFIED | 1 |
| PY-UE-003 | 2026-08-27 | Unreal Python / GameFeatureData | `Failed to find property 'input_mappings'` | STATIC_ONLY | 1 |
| PY-UE-004 | 2026-08-28 | Unreal Editor-Cmd / DDC 启动 | `no writable nodes available` | VERIFIED | 1 |
| PY-UE-005 | 2026-08-28 | Unreal Python / 资产布局迁移 | `FindAssetPackageReferencers failed` / `AsyncLoading2.cpp !bHasFailed` | VERIFIED | 2 |
| PY-UE-006 | 2026-08-28 | Unreal Commandlet / Interchange 导入 | `SlateApplication CurrentApplication.IsValid()` | VERIFIED | 1 |
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
- 预防规则：`Cmd` 模式执行文件只使用 `py "C:/.../script.py"`；或用编辑器的 Execute Python Script 文件入口。
- 修复：改用 `py "C:/EpicWkspc/OceanAdventure/Plugins/GameFeatures/TopDownFeature/Content/Python/create_top_down_assets.py"`。
- 验证：下一次运行进入脚本并输出 Python traceback，证明入口已正确切换到 Python。
- 状态：`VERIFIED`。

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

- 日期：2026-08-28；发生一次。
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
