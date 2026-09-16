# SkyLand 玩家控制迁移验证协议

覆盖 [`MigrateSkyLandPlayerControl.py`](../../Plugins/GameFeatures/OceanAdventure/Content/Python/MigrateSkyLandPlayerControl.py)
产出的输入资产与 Pawn 移动数值。设计与换算表见
[`../tech/SkyLand_玩家控制迁移方案.md`](../tech/SkyLand_玩家控制迁移方案.md)。

> **本协议尚未执行过。** 脚本目前只通过了 `ast.parse` 与 `Tools/check_absolute_paths.py`，
> 状态是 `STATIC_ONLY`——没有任何一行在 UE 宿主里跑过。

分三段：

- **A. 脚本运行与幂等**——机器判定，有成功标记；
- **B. 资产读回**——机器判定，一段只读探针；
- **C. 手感目视验收**——人判定，但每条都给了可一眼判定的判据。

C 不能被 A/B 代替：A/B 证明数值写进了资产，**证明不了按键真的到达了角色**。

---

## 前置条件

五条都满足才开始。任一条不满足，回报 `BLOCKED` 并注明是哪条。

1. 要验的那份代码在你的分支上。检查：

   ```powershell
   git ls-files "Plugins/GameFeatures/OceanAdventure/Content/Python/MigrateSkyLandPlayerControl.py" `
                "doc/tech/SkyLand_玩家控制迁移方案.md"
   git grep -c "InputTag.Player.Sprint" -- Config/DefaultGameplayTags.ini
   ```

   **应列出 2 个文件，且计数为 1。** 少任何一项说明分支不完整，`git pull` 不解决问题——
   见 `AGENTS.md`「修复必须落到 main」。

2. 编译通过。测试入口不负责构建：

   ```powershell
   if (-not $env:UE_ROOT) { throw '请先设置 UE_ROOT 为本机 UE 安装根目录' }
   & (Join-Path $env:UE_ROOT 'Engine/Build/BatchFiles/Build.bat') LyraEditor Win64 Development "-Project=$PWD\LyraTemplate.uproject" -WaitMutex -NoHotReloadFromIDE
   ```

   预期末尾 `Result: Succeeded`。本脚本用到 `OceanAdventureAssetLibrary.create_add_input_context_mapping_action`，
   它在 `OceanAdventureRuntime` 里；旧 DLL 会让脚本报「unreal.OceanAdventureAssetLibrary is missing」。

3. `CreateOceanAdventureExperience.py` 已经跑过，这三个资产在：
   `/OceanAdventure/Input/DA_InputConfig_OceanAdventure`、
   `/OceanAdventure/Character/BP_OceanAdventure_Pawn`、
   `/OceanAdventure/OceanAdventure`。缺任何一个，脚本会点名报错，照抄回报即可。

4. `InputTag.Player.Sprint` 已注册。它在第 1 条里检查过，但**注册进 ini 之后必须重启过一次编辑器**，
   否则 Tag 表还是旧的，脚本会报 `GameplayTag is not registered`。

5. 按路线准备宿主：
   - **走 A（命令行，推荐）**：设好 `UE_ROOT`，关掉已打开的编辑器；
   - **走 A′（编辑器内）**：编辑器已启动、**不在 PIE 中**（没点 Play）。脚本自己会挡 PIE，
     但挡住就等于这一轮白跑；
   - **走 C（目视）**：必须是完整编辑器。

---

## A. 脚本运行与幂等

### A-1 首次运行

```powershell
& (Join-Path $env:UE_ROOT 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') "$PWD\LyraTemplate.uproject" -run=pythonscript -script="MigrateSkyLandPlayerControl.py" -DDC-ForceMemoryCache -unattended -stdout -FullStdOutLogOutput
```

`-DDC-ForceMemoryCache` 不是可选项：受限环境下默认 DDC 图没有可写节点，引擎会在进入
Python 之前就 `appError` 退出（`python-script-governance` 的 `PY-UE-004`）。

**A′（编辑器内）**：Output Log 输入框左侧的下拉切到 **Python 输入模式**（不是 `Cmd`），执行：

```text
import MigrateSkyLandPlayerControl
```

`Cmd` 模式会把这行当引擎控制台命令**静默丢掉**，零输出零报错，看起来像「跑了但没做事」
（`PY-UE-002` 的第二次发生）。同一会话内再跑一次用：

```text
import importlib, MigrateSkyLandPlayerControl
importlib.reload(MigrateSkyLandPlayerControl)
```

### A-2 再跑一次（幂等）

用与 A-1 完全相同的入口再跑一遍。**只跑一次看不出累加缺陷**：数组过滤一旦退化成追加，
第一次运行的结果是正确的，第二次才开始长。

### 预期

| # | 判据 |
| --- | --- |
| A-1a | 末行出现 `SKYLAND_PLAYER_CONTROL_OK`。**没有这一行就等于没成功**，哪怕前面没报错 |
| A-1b | 无 Python traceback |
| A-1c | 日志里有 `NativeInputActions: kept <N>, owns 1` 与 `AbilityInputActions: kept <M>, owns 2` |
| A-1d | 日志里有一行 `AUDIT:` 开头的结论。若是 `AUDIT SKIPPED`，照抄，不算失败 |
| A-2a | 同样出现 `SKYLAND_PLAYER_CONTROL_OK` |
| A-2b | `kept` 的两个数字与 A-1 **完全相同**。变大即说明过滤退化成了追加，是失败 |

---

## B. 资产读回

A 通过后，在**编辑器的 Python 输入模式**执行这段只读探针（不改任何资产）：

```python
import unreal

config = unreal.EditorAssetLibrary.load_asset("/OceanAdventure/Input/DA_InputConfig_OceanAdventure")
natives = list(config.get_editor_property("native_input_actions"))
abilities = list(config.get_editor_property("ability_input_actions"))
print("READBACK_NATIVE_COUNT=%d ABILITY_COUNT=%d" % (len(natives), len(abilities)))
for entry in natives + abilities:
    action = entry.get_editor_property("input_action")
    print("  %s -> %s" % (action.get_name() if action else "None",
                          entry.get_editor_property("input_tag").export_text()))

pawn = unreal.EditorAssetLibrary.load_blueprint_class("/OceanAdventure/Character/BP_OceanAdventure_Pawn")
move = unreal.get_default_object(pawn).get_component_by_class(unreal.CharacterMovementComponent)
for name in ("max_walk_speed", "max_acceleration", "braking_deceleration_walking",
             "braking_friction", "ground_friction", "jump_z_velocity", "gravity_scale",
             "air_control", "max_step_height", "walkable_floor_angle", "walkable_floor_z"):
    print("READBACK_MOVE %s=%s" % (name, move.get_editor_property(name)))
```

### 预期

| # | 判据 |
| --- | --- |
| B-1 | `IA_Player_Sprint -> InputTag.Player.Sprint` 出现，且**只出现一次** |
| B-2 | `IA_Player_Interact -> InputTag.Ability.Interact` 与 `IA_Player_Drop -> InputTag.Ability.Quickslot.Drop` 各出现一次 |
| B-3 | Lyra 原有的 `IA_Interact -> InputTag.Ability.Interact` **仍在**（E 键没被顶掉） |
| B-4 | 九个数值分别为 `320 / 2800 / 2400 / 0 / 0 / 700 / 2.2449 / 0.2429 / 35` |
| B-5 | `walkable_floor_angle=60`，且 `walkable_floor_z` 约等于 `0.5`。两者不配套说明派生值没跟着走 |

---

## C. 手感目视验收

进 PIE，用 `L_OceanAdventure` 之类带 Ocean Experience 的关卡。

| # | 操作 | 判据 |
| --- | --- | --- |
| C-1 | WASD 走动 | 角色朝鼠标方向，走直线；满速时松手**约 0.13 秒**停住，不滑 |
| C-2 | 满速直线跑，突然反向 | **约 0.23 秒**完成掉头；中间不应有明显的「拖泥带水」段 |
| C-3 | 空格起跳 | 跳起高度约 1.1 m（能上一层 1 m 台地）；下落比 Lyra 默认更快（重力 2.24 倍） |
| C-4 | 空中按方向键 | 能小幅修正落点，但明显弱于地面转向 |
| C-5 | 按 F | 触发交互，与按 E 表现一致 |
| C-6 | 按 Q | 触发丢弃。当前 Experience 未授予快捷栏 GA 时无反应——**这条不算失败**，照实回报 |
| C-7 | 按住 Shift | **预期无反应**。冲刺消费方还没写，见迁移方案第四节。有反应反而要回报 |
| C-8 | 控制台 `showdebug enhancedinput` 后按 F/Q/Shift | 三个 IA 都应显示为已触发，且**不带 `OVERRIDDEN BY`**。出现覆盖说明优先级 2 没生效 |

---

## 报告模板

整段填完贴回，不要只回「通过」或「有问题」。

```text
状态：PASS / FAIL / BLOCKED
引擎：UE 5.__.__      路线：A（命令行） / A′（编辑器内）
分支与提交：

A-1  [ ] 成功标记   [ ] 无 traceback   kept: native=__ ability=__
     AUDIT 行原文：
A-2  [ ] 成功标记   kept: native=__ ability=__（应与 A-1 相同）
B    READBACK 原文（整段贴）：
C    C-1 __  C-2 __  C-3 __  C-4 __  C-5 __  C-6 __  C-7 __  C-8 __

FAIL 时：从第一条报错到结尾的原始输出（不转述、不节选）：
BLOCKED 时：哪一条前置没满足，以及当时的输出：
```
