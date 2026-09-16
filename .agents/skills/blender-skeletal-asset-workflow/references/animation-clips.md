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

## 3. 一个 FBX 还是几个：按资产复杂度分

网格永远单独一个 FBX（`object_types={"ARMATURE","MESH"}`、`bake_anim=False`）。
动画分几个文件，判据是**这些 clip 是不是一起生成、一起改**：

| | 合并成一个动画 FBX | 一个 clip 一个 FBX |
|---|---|---|
| 什么时候 | **单一物件、动画不复杂**：clip 少、都驱动同一组骨骼、由同一个脚本一次产出（弓、门、炮闩、宝箱） | **角色或复杂资产**：clip 会被单独重导、多人分工、需要固定资产名——按 UE5 的常规习惯来 |
| Blender | `bake_anim_use_all_actions=True`，一次导出，每个 Action 一个 animation stack | `bake_anim_use_all_actions=False`，逐个 Action 各导一个文件 |
| UE 资产名 | **由导入器按 stack 名派生**，脚本不能假定路径 | 由 `AssetImportTask.destination_name` 决定，稳定可引用 |
| 代价 | 省文件、改一次全同步；命名权交给引擎 | 名字稳、能单独重导；文件多，漏导一个不容易发现 |

先例：`create_wood_bow.py` 走合并（四段 clip、三根驱动骨、一个脚本一次产出），
导出 `blender/models/A_WoodBow_Clips.fbx`；它保留 `BUNDLE_CLIPS_IN_ONE_FBX` 开关，
关掉就退回每 clip 一个文件。

**选了合并，就必须补这两条校验**，否则合并省下的那点文件数会用一次静默事故还回来：

- **Blender 侧**：导出后把 FBX 当字节读，逐个确认 Action 名（stack 名是 ASCII）都在里面。
  少一段的包和完整包在「文件存在、大小合理」上完全一样。
- **UE 侧**：导入后用 Asset Registry **发现**本骨架下的 AnimSequence，按 Action 名认领；
  数量不足、或一个 clip 匹配到多个资产，都要停——猜一个就是把 AnimBlueprint 接到别的 clip 上。

〔未验证：UE 5.7 从单文件多 stack 究竟产出几个 AnimSequence、按什么规则命名，尚未在宿主跑过。
`CreateWoodBowAssets.py` 因此不假定资产名，并在数量不足时直接指向
`BUNDLE_CLIPS_IN_ONE_FBX = False` 的回退路径。〕

导出动画 FBX 时（两种布局都适用）：

- `bake_anim_use_nla_strips=False`；
- `bake_anim_force_startend_keying=True`，保证首尾帧有键；
- `bake_anim_step=1.0`、`bake_anim_simplify_factor=0.0`（第 2 节）；
- `scene.frame_start` / `frame_end` 覆盖要导的 Action 范围；
- 其余参数展开 `UE_FBX_COMMON`（`SKILL.md` 2.5）。

合并导出会把**当前文件里所有能作用于该骨架的 Action** 都写进去，所以重跑前要按名字
（连 `.001` 后缀一起）删干净自己上一轮的 Action，否则旧的会作为多出来的一段混进包里。

导完别把 `.blend` 留在摆着的状态。挂一段 idle 之类的静止 clip 并把播放头停在它的第 0 帧，
比把 `animation_data.action` 置空更好——置空后动作编辑器显示「新建」，
和「一段都没烘出来」在界面上分不出来。

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
