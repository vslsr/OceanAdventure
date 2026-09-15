# 动画 clip：曲线移植、采样率、UE 导入

`SKILL.md` 第 3 节的展开。只在真正要烘 clip 时读。

## 1. 曲线写成纯函数

手感曲线不要直接写在摆姿势的代码里。抽成不认识 `bpy`、不认识骨骼的纯函数 ——
输入一个量或一个时间，输出一个数 —— 然后由烘焙循环去采样它。

这样做有三个实际好处，第三个才是关键：

1. 改手感是改常量，不是手摆关键帧；
2. 同一组曲线可以同时喂给 clip 烘焙和脚本内校验；
3. **它们可以在没有 Blender 的机器上跑断言**（`SKILL.md` 第 4 节第 1 层）。

`create_wood_bow.py` 里的那组（拉弓 / 撒手）是从 SkyLand 的
`src/render/RenderWeaponDraw.ts` 原样移植的，可作样板。

### 回弹这类曲线要拆成两项

「归零」和「抖动」写成一条衰减曲线时，归零那一段会把过冲吃掉 —— 看上去只是慢慢松回去，
没有那一记回弹。分开写：

```python
progress = clamp01(elapsed / DURATION)
snap_back = start * max(0.0, 1.0 - progress / RETURN_RATIO)
wobble = -OVERSHOOT * math.sin(2*math.pi*WOBBLES*progress) * math.exp(-DAMPING*elapsed)
return snap_back + wobble
```

`wobble` 允许取负值：那是穿过静止位弹到前面去，也是整段动作里唯一让人认出「它弹回来了」
的部分。校验必须显式断言这个负值出现过，否则曲线被压平时没有任何征兆。

### 多段动作之间必须端点连续

后一段的起点要严格等于前一段的终点（撒手从满弓开始）。差一点，切换那一帧就会瞬移，
而那往往恰好是玩家一定在看的一帧。写成断言，不要靠眼睛看。

## 2. 采样率按最高频成分定，不要按 60fps 拍脑袋

回弹这类动作的频率是 `WOBBLES / DURATION`。0.12 秒里穿越两次就是约 17Hz，
60fps 每周期不到四个采样，会把回弹混叠成一下抽搐。

取到每周期 ≥ 4 个采样再留余量。弓那个例子用 120fps，每周期 7.2 个采样，
烘完过冲保住 -0.0122m（连续解 -0.0123m）。代价只是十几个关键帧。

三处都要一致，缺一处前功尽弃：

| 位置 | 设置 |
|---|---|
| Blender 场景 | `scene.render.fps`（导出器按它给 baked 动画计时，**烘之前**就要设好） |
| Blender 导出 | `bake_anim_step=1.0`、`bake_anim_simplify_factor=0.0` |
| UE 导入 | `use_default_sample_rate=False` + `custom_sample_rate=<同一个值>` |

`bake_anim_simplify_factor` 非零会做曲线拟合，它最先丢掉的正是「短、小、快」那类运动
—— 也就是回弹本身。

## 3. 一个 clip 一个 FBX

网格一个 FBX（`object_types={"ARMATURE","MESH"}`、`bake_anim=False`），
每段动画各一个（`object_types={"ARMATURE"}`、`bake_anim=True`）。
多 take 塞一个 FBX 在 UE 侧支持有限，不值得省这一个文件。

导出动画 FBX 时：

- `bake_anim_use_nla_strips=False`、`bake_anim_use_all_actions=False`，
  只导当前 `animation_data.action`；
- `bake_anim_force_startend_keying=True`，保证首尾帧有键；
- `scene.frame_start` / `frame_end` 设成该 Action 的范围；
- 其余参数展开 `UE_FBX_COMMON`（`SKILL.md` 2.5）。

导完把 `animation_data.action` 置空、姿势清干净，别把 `.blend` 留在摆着的状态。

## 4. UE 导入侧

```python
options.set_editor_property("import_mesh", False)
options.set_editor_property("import_as_skeletal", True)
options.set_editor_property("import_animations", True)
options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
options.set_editor_property("skeleton", skeleton)      # 必须，见 SKILL.md 第 5 节
anim = options.get_editor_property("anim_sequence_import_data")
anim.set_editor_property("import_bone_tracks", True)
anim.set_editor_property("remove_redundant_keys", False)
anim.set_editor_property("use_default_sample_rate", False)
anim.set_editor_property("custom_sample_rate", FPS)
```

〔未验证：以上属性名按 `UFbxAnimSequenceImportData` 反射推得，尚未在 UE 宿主跑过。
首次运行若报属性不存在，按门禁先留档再改，不要静默跳过。〕

导入后的读回校验，两条都要：

- **clip 绑的 Skeleton 是不是这一个**。绑错的 AnimSequence 加载正常、就是永远不播，
  比较包路径而不是相信导入器绑对了。
- **时长对不对**。时长是唯一一个在设计侧有明确数值的量，因此是唯一能证明
  「往返 FBX 之后时间轴没被改掉」的东西。

## 5. AnimBlueprint 侧（脚本管不到的部分）

动画图节点没有 Python 暴露，这一段是手工连线。脚本能做的是保证它依赖的骨骼、
Socket、clip 确实存在且没被压平。

连线时记住两类 clip 的区别（`SKILL.md` 第 3 节）：姿势斜坡用 Sequence Evaluator
按 `量 * length` 显式采样；表演用 Sequence Player 播一次。把姿势斜坡接到 Player 上
是这一步最容易犯的错 —— 弓会自己拉开又自己松开，跟玩家的蓄力毫无关系。
