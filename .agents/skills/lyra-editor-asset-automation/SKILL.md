---
name: lyra-editor-asset-automation
description: 为 OceanAdventure 的 UE 5.7/Lyra 编辑器资产脚本配置 AbilitySet、InputConfig 或 GameFeatureData，并处理 Python USTRUCT 构造失败、EditDefaultsOnly 不可写、GameplayTag 读回误判和原生编辑器桥接编译错误。用户要求用 Python 创建或修复这些 Lyra 资产，或日志出现 `call() takes at most 0 arguments`、`cannot be edited on instances`、`InputConfig did not retain`、`SubclassOf.h` 未定义类型时使用；普通运行时玩法 C++、可直接编辑的 Python 资产属性和 Blender 脚本不使用本技能。
---

# Lyra 编辑器资产自动化

让幂等 Python 脚本继续作为资产修改入口；只有 Unreal Python 无法表达的 Lyra 反射写入才下沉为所属 GameFeature 内的窄原生编辑器桥接。不要手工改写 `.uasset` 二进制。

## 先判断是否需要原生桥接

1. 先确认目标属性能否直接通过 `set_editor_property` 写入，或目标 USTRUCT 是否支持关键字构造。
2. 根据报错停止无效尝试：
   - `call() takes at most 0 arguments`：该 Python USTRUCT 包装器不支持带参数构造。
   - `Property ... cannot be edited on instances`：字段是 `EditDefaultsOnly`，Python 实例不能逐字段修改。
3. UE 5.7 本项目中：
   - `LyraInputAction` 的实例字段写入已触发 `cannot be edited on instances`；当前脚本改用 `unreal.LyraInputAction(input_action=..., input_tag=...)` 关键字构造，并必须通过实际脚本运行验证该引擎版本的包装器。
   - `unreal.LyraAbilitySet_GameplayAbility` 已拒绝带参数构造，其字段又是 `EditDefaultsOnly`；配置 `ULyraAbilitySet::GrantedGameplayAbilities` 时使用原生桥接。
4. 只有遇到上述反射限制才增加桥接。普通可编辑属性继续由 Python 直接处理。

桥接和调用脚本必须留在拥有该玩法的 GameFeature。本项目的 Lyra 输入与 AbilitySet 属于 `OceanAdventure`；不要把 LyraGame/GAS 依赖放进 `BuildingCore`，也不要让 `Raft` 依赖玩家玩法层。

## 实现 AbilitySet 桥接

在 GameFeature 的编辑器资产函数库中暴露一个窄 `UFUNCTION`，接收 `ULyraAbilitySet*` 和长度一致的能力类、等级、输入 Tag 数组。实现时遵守以下顺序：

1. 在真正使用 `TSubclassOf<ULyraGameplayAbility>` 转换或布尔判断的 `.cpp` 中包含 `AbilitySystem/Abilities/LyraGameplayAbility.h`。只有前置声明会在 `SubclassOf.h` 实例化 `T::StaticClass()` 时导致未定义类型错误。
2. 包含 `AbilitySystem/LyraAbilitySet.h` 与 `UObject/UnrealType.h`。
3. 用 `FindFProperty<FArrayProperty>` 查找 `GrantedGameplayAbilities`，再把 `Inner` 检查为 `FStructProperty`。
4. 修改前必须验证 `EntryProperty->Struct == FLyraAbilitySet_GameplayAbility::StaticStruct()`；布局不符就记录错误并返回，不能继续解释原始内存。
5. 修改前一次性验证资产、三个数组长度、能力类、等级和 GameplayTag。失败时保持原资产不变。
6. 调用 `Modify()`，通过 `ContainerPtrToValuePtr` 和 `FScriptArrayHelper` 清空并重建数组。
7. `FScriptArrayHelper::GetRawPtr()` 返回 `uint8*`。在第 4 步已验证确切结构类型后，使用 `reinterpret_cast<FLyraAbilitySet_GameplayAbility*>`；`static_cast` 会因类型无继承关系而编译失败。
8. 填入 `Ability`、`AbilityLevel`、`InputTag`，最后调用 `MarkPackageDirty()` 并返回成功。

Python 侧收集三个并行数组，调用桥接后检查布尔返回值，再保存资产。脚本重跑时仍需遵守仓库已有的稳定 Action 名称、保留手工 Action 和注册表 GameplayTag 规则。

本项目的实现与复查锚点是：

- `Plugins/GameFeatures/OceanAdventure/Source/OceanAdventureRuntime/Private/Editor/OceanAdventureAssetLibrary.cpp` 中的 `ConfigureAbilitySetGameplayAbilities`；
- `Plugins/GameFeatures/OceanAdventure/Content/Python/CreateOceanAdventureBuildAssets.py` 中对该函数的调用与返回值检查。

后续修改应检查这些锚点的现状，不要复制出第二套桥接。

## UE 5.7 Python GameplayTag 读回陷阱

`InputConfig did not retain IA_... -> <Struct 'GameplayTag' ... {}>` 不一定表示写入失败。UE 5.7 的 Python `FGameplayTag` 包装器可能让 `left == right` 比较包装器身份，而不是底层 Tag 值；即使 `LyraInputAction(input_action=..., input_tag=...)` 已正确写入，直接用 `entry_tag == input_tag` 仍会误报。

配置 `ULyraInputConfig::NativeInputActions` 或 `AbilityInputActions` 时固定采用以下规则：

1. 保留 `unreal.LyraInputAction(input_action=action, input_tag=input_tag)` 关键字构造，不要在实例上写 `EditDefaultsOnly` 字段。
2. 从注册表取 Tag 时先尝试 `GameplayTagLibrary.request_gameplay_tag(unreal.Name(name), False)`；若 UE Python 暴露中不存在该函数（`AttributeError`），用 `unreal.GameplayTag().import_text(name)` 兼容读取，再用 `is_gameplay_tag_valid(tag)` 验证。不要读取不存在的 `tag_name`。
3. 写入后读回比较优先调用 `GameplayTagLibrary.equal_equal_gameplay_tag(left, right)`；若当前 Python 暴露中没有该函数，比较两者 `export_text()` 结果，最后才退回 `==`。这是语义比较，不能把 `repr` 中的 `{}` 当作无效依据，也不能省略为直接 `==`。
4. UObject 资产同样不要直接 `==`；用 `get_path_name()` 并去掉导出对象后缀（`.` 后内容）比较稳定包路径。

首次创建资产时，先 `scan_paths_synchronous`，再以 `EditorAssetLibrary.does_asset_exist(package_path)` 守卫 `load_asset`。这样不会在资产确实不存在的首次运行中制造预期的 `LoadAsset failed` 噪声；找到后再读回验证并保存。

本次复查证据：UE 5.7 Python commandlet 使用 `-DDC-ForceMemoryCache` 执行 `CreateOceanAdventureExperience.py`，日志包含 `Python script executed successfully`，并成功保存四个 WASD InputAction、三个相机 InputAction、IMC 与 InputConfig。命令进程仍可能因本机 DDC 无可写节点或 ONNX runtime 缺失返回非零；应以 `LogPythonScriptCommandlet` 的 Python 结果和资产保存记录区分脚本故障与环境告警。

## 故障速查

| 现象 | 根因 | 修复 |
| --- | --- | --- |
| `call() takes at most 0 arguments` | USTRUCT Python 包装器不支持参数构造 | 能用关键字构造的结构改用关键字；AbilitySet 条目改走原生桥接 |
| `Property ... cannot be edited on instances` | `EditDefaultsOnly` 阻止 Python 修改结构实例 | 不调用实例 `set_editor_property`；改用已支持的关键字构造或原生桥接 |
| `SubclassOf.h`: 使用了未定义类型 `ULyraGameplayAbility` | `.cpp` 触发了 `TSubclassOf` 内联实例化但只有前置声明 | 在 `.cpp` 包含 `LyraGameplayAbility.h` |
| 无法从 `uint8*` `static_cast` 到条目结构 | 反射数组返回原始字节指针，类型无继承关系 | 先核对 `FStructProperty::Struct`，再 `reinterpret_cast` |
| Python 中没有新函数或仍调用旧签名 | 编辑器仍加载旧 DLL | 成功编译目标后完整重启编辑器，再运行脚本 |
| `AttributeError: GameplayTagLibrary has no attribute request_gameplay_tag` | 当前 UE Python 暴露未提供注册表请求函数 | 用 `GameplayTag().import_text(name)` 兜底，并用 `is_gameplay_tag_valid` 验证 |
| `RuntimeError: InputConfig did not retain IA_...`，但 Tag 已注册 | `FGameplayTag` Python 包装器的 `==` 可能只比较对象身份 | 用 `equal_equal_gameplay_tag` 或 `export_text()` 做语义比较；用 `is_gameplay_tag_valid` 判定有效性 |
| 首次运行出现一串 `LoadAsset failed` | 脚本对尚未创建的资产直接调用 `load_asset` | `scan_paths_synchronous` 后先用 `does_asset_exist` 守卫加载 |

## 构建与验证门禁

1. 使用项目编辑器目标做完整 C++ 构建，不以 Python 语法检查代替编译：

   ```text
   <UE_5.7>/Engine/Build/BatchFiles/Build.bat LyraEditor Win64 Development "-Project=<repo>/LyraTemplate.uproject" -WaitMutex -NoHotReloadFromIDE
   ```

2. 只有构建输出包含 `Result: Succeeded`，且 `UnrealEditor-OceanAdventureRuntime` 已链接，才算原生桥接通过。
3. 完整重启编辑器，让 Python 反射注册新 `UFUNCTION`；运行所属 GameFeature 的资产脚本。
4. 脚本必须无 traceback，并读回验证所拥有的资产项或 GameFeature Actions。若脚本修改了 GameFeatureData Actions，再重启一次编辑器后进入 PIE。
5. 最后运行 Python AST/语法检查与 `git diff --check`。这些是补充检查，不能替代第 1 步。

出现反射布局变化、数组输入不一致、无效能力类或无效 Tag 时应停止并保留原资产，不能为了让脚本继续而跳过验证。
