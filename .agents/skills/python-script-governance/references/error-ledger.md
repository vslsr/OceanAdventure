# Python 失败档案

本文件是项目 Python 脚本的追加式失败记录。开始任何 Python 创建、修改、审计、排查或执行任务前必须完整阅读。新失败必须在下一次脚本修改前记录；相同签名复发时更新原条目，不创建重复条目。

## 索引

| ID | 首次发生 | 领域 | 错误签名 | 状态 | 次数 |
| --- | --- | --- | --- | --- | ---: |
| PY-UE-001 | 2026-08-27 | Unreal Python / SceneComponent | `set_relative_rotation() required argument 'sweep'` | VERIFIED | 2 |
| PY-UE-002 | 2026-08-27 | Unreal Editor / 执行入口 | `尝试执行已废弃的命令：exec(open(...))` | VERIFIED | 1 |
| PY-UE-003 | 2026-08-27 | Unreal Python / GameFeatureData | `Failed to find property 'input_mappings'` | STATIC_ONLY | 1 |
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
