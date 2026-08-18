# Claude + Blender Python 脚本规范

兼容范围：Blender 3.6 ～ 4.4+

---

## 一、脚本执行模板

所有脚本的标准入口结构，解决从文本编辑器运行时的上下文错误。

```python
import bpy
import math

# 清空场景（直接 API，不走 bpy.ops，无需视口上下文）
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)

# 清空历史 Action（防止重复运行残留旧动画被一起导出到 FBX）
for action in list(bpy.data.actions):
    bpy.data.actions.remove(action)

# 清空孤立材质（防止重复运行产生重名材质）
for mat in list(bpy.data.materials):
    if mat.users == 0:
        bpy.data.materials.remove(mat)

def main():
    # ── 所有建模逻辑写在这里 ──
    pass

# 自动查找 3D 视口并注入上下文，解决 poll() context is incorrect
def run():
    for window in bpy.context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == 'VIEW_3D':
                for region in area.regions:
                    if region.type == 'WINDOW':
                        with bpy.context.temp_override(
                            window=window, area=area, region=region
                        ):
                            main()
                        return
    raise RuntimeError("未找到 3D 视口，请确认 Scripting 工作区包含 3D 视口面板")

run()
```

---

## 二、材质

### 查找 Principled BSDF 节点
不用名称字符串（随 UI 语言变化），用节点类型。

```python
def get_bsdf(mat):
    for node in mat.node_tree.nodes:
        if node.type == 'BSDF_PRINCIPLED':
            return node
    return None
```

### 访问节点输入插槽
不用数字索引（4.x 重排后失效），用名称 + `.get()`。

```python
base = bsdf.inputs.get("Base Color") or bsdf.inputs.get("Color")
if base:
    base.default_value = (r, g, b, 1.0)
```

### 完整材质创建模板

```python
def make_material(name, color, metallic=0.0, roughness=0.5, alpha=1.0):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    bsdf = get_bsdf(mat)
    if bsdf is None:
        return mat
    base = bsdf.inputs.get("Base Color") or bsdf.inputs.get("Color")
    if base:
        base.default_value = (*color, 1.0)
    m = bsdf.inputs.get("Metallic")
    if m:
        m.default_value = metallic
    r = bsdf.inputs.get("Roughness")
    if r:
        r.default_value = roughness
    if alpha < 1.0:
        a = bsdf.inputs.get("Alpha")
        if a:
            a.default_value = alpha
        try:
            mat.blend_method = 'BLEND'   # 4.2+ 已移除，忽略错误
        except AttributeError:
            pass
    return mat
```

---

## 三、动画

### 动画规范：每个骨骼网格必须包含 Idle

每个导出的骨骼网格至少需要两个 Action：

| Action | 命名规则 | 插值 | 内容 |
|--------|---------|------|------|
| **Idle** | `{模型名}_Idle` | CONSTANT | 默认静止状态，首尾关键帧相同，什么都不做 |
| 功能动画 | `{模型名}_{动作}` | BEZIER | 实际动作，如 Open / Close / Attack |

```python
def set_interpolation(action, mode='BEZIER'):
    for fc in get_fcurves(action):
        for kp in fc.keyframe_points:
            kp.interpolation = mode

# animation_data 默认为 None，直接赋值会报 AttributeError，必须先初始化
armature.animation_data_create()

# ── Idle Action（必须，默认状态）──
idle_action = bpy.data.actions.new(name="MyModel_Idle")
armature.animation_data.action = idle_action
# 首尾帧相同，保持默认姿势不动
scene.frame_set(1);  pose_bone.rotation_euler.x = math.radians(DEFAULT_ANGLE)
pose_bone.keyframe_insert(data_path="rotation_euler", frame=1)
scene.frame_set(24); pose_bone.rotation_euler.x = math.radians(DEFAULT_ANGLE)
pose_bone.keyframe_insert(data_path="rotation_euler", frame=24)
set_interpolation(idle_action, 'CONSTANT')  # 静止用 CONSTANT

# ── 功能 Action（按需添加）──
close_action = bpy.data.actions.new(name="MyModel_Close")
armature.animation_data.action = close_action
scene.frame_set(1);  pose_bone.rotation_euler.x = math.radians(OPEN_ANGLE)
pose_bone.keyframe_insert(data_path="rotation_euler", frame=1)
scene.frame_set(24); pose_bone.rotation_euler.x = math.radians(CLOSE_ANGLE)
pose_bone.keyframe_insert(data_path="rotation_euler", frame=24)
set_interpolation(close_action, 'BEZIER')   # 动作用 BEZIER 缓入缓出
```

> ⚠️ 多个 Action 必须配合脚本开头 `bpy.data.actions` 清空使用，
> 导出参数 `bake_anim_use_all_actions=True`，确保每个 Action 都被导出。

### FCurves 访问（4.4+ Layered Action 兼容）

```python
def get_fcurves(action):
    """兼容 3.x/4.0-4.3（legacy）和 4.4+（layered action）"""
    if hasattr(action, 'fcurves'):       # 3.x / 4.0-4.3
        return list(action.fcurves)
    fcurves = []                         # 4.4+ layered action
    if hasattr(action, 'layers'):
        for layer in action.layers:
            for strip in layer.strips:
                if hasattr(strip, 'channelbags'):
                    for bag in strip.channelbags:
                        fcurves.extend(bag.fcurves)
    return fcurves
```

### 设置关键帧插值

```python
if obj.animation_data and obj.animation_data.action:
    for fc in get_fcurves(obj.animation_data.action):
        for kp in fc.keyframe_points:
            kp.interpolation = 'BEZIER'  # 缓入缓出
```

### 铰链动画模板（Empty 驱动，仅 Blender 预览用）

```python
# 铰链空物体作为旋转轴心
bpy.ops.object.empty_add(type='PLAIN_AXES', location=(hx, hy, hz))
hinge = bpy.context.object
hinge.name = "Hinge"

# 子物体绑定到铰链
bpy.ops.object.select_all(action='DESELECT')
for obj in child_objects:
    obj.select_set(True)
hinge.select_set(True)
bpy.context.view_layer.objects.active = hinge
bpy.ops.object.parent_set(type='OBJECT', keep_transform=True)

# 关键帧
hinge.rotation_mode = 'XYZ'
scene.frame_set(1);  hinge.rotation_euler.x = math.radians(0);   hinge.keyframe_insert(data_path="rotation_euler", frame=1)
scene.frame_set(24); hinge.rotation_euler.x = math.radians(110); hinge.keyframe_insert(data_path="rotation_euler", frame=24)
```

---

## 四、骨骼动画（UE5 导出用）

### Armature 创建与骨骼动画模板

```python
# 创建 Armature
bpy.ops.object.armature_add(location=(hx, hy, hz))
armature = bpy.context.object
armature.name = "MyArmature"
armature.data.name = "MyBones"

bpy.ops.object.mode_set(mode='EDIT')

# Root 骨骼（必须有，UE3/UE5 通用约定）
root_bone = armature.data.edit_bones[0]
root_bone.name = "Root"
root_bone.head = (0.0, 0.0, 0.0)
root_bone.tail = (0.0, 0.0, 0.1)

# 功能骨骼作为 Root 的子骨骼
child_bone = armature.data.edit_bones.new(name="MyBone")
child_bone.head = (hx, hy, hz)   # 铰链 / 旋转轴心位置
child_bone.tail = (hx, hy, hz + height)
child_bone.parent = root_bone     # ← 必须挂在 Root 下

bpy.ops.object.mode_set(mode='OBJECT')

# 绑定网格（直接 Python 赋值，不走 bpy.ops，最可靠）
# ❌ 不用 bpy.ops.object.parent_set(type='ARMATURE_NAME')
#    该方法不保证 Armature 修改器的 object 指向正确，导致网格不跟随骨骼

# 1. 设置父子关系
mesh_obj.parent = armature
mesh_obj.parent_type = 'OBJECT'
mesh_obj.matrix_parent_inverse = armature.matrix_world.inverted()

# 2. 手动添加 Armature 修改器并明确指向骨骼
mod = mesh_obj.modifiers.new(name="Armature", type='ARMATURE')
mod.object = armature
mod.use_vertex_groups = True

# 3. 全顶点权重赋给目标骨骼（刚体绑定，无需权重绘制）
vg = mesh_obj.vertex_groups.new(name='MyBone')
vg.add(list(range(len(mesh_obj.data.vertices))), 1.0, 'REPLACE')

# Pose 模式添加关键帧
bpy.ops.object.select_all(action='DESELECT')
armature.select_set(True)
bpy.context.view_layer.objects.active = armature
bpy.ops.object.mode_set(mode='POSE')

pose_bone = armature.pose.bones["MyBone"]
pose_bone.rotation_mode = 'XYZ'

scene.frame_set(1);  pose_bone.rotation_euler.x = math.radians(0)
pose_bone.keyframe_insert(data_path="rotation_euler", frame=1)
scene.frame_set(24); pose_bone.rotation_euler.x = math.radians(110)
pose_bone.keyframe_insert(data_path="rotation_euler", frame=24)

bpy.ops.object.mode_set(mode='OBJECT')
```

---

## 五、网格合并

### 合并为单一静态网格

```python
# 收集目标对象并合并，必须在创建其他对象之前执行
parts = [o for o in bpy.context.scene.objects if o.type == 'MESH']
bpy.ops.object.select_all(action='DESELECT')
for o in parts:
    o.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
merged = bpy.context.object
merged.name = "MergedMesh"

# UV 展开（合并后必须做，ActorX / UE3 导出 PSK 要求有 UV Map）
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.02)
bpy.ops.object.mode_set(mode='OBJECT')
```

> ⚠️ `join()` 执行后，原对象引用全部失效（ReferenceError）。
> 合并顺序：**先合并静态部件 → 再创建需要独立引用的动态部件。**
> UV 展开必须在合并之后、导出之前完成，否则 ActorX 报 "No valid skin triangles"。

---

## 六、导出到 UE5

### 导出路径约定

所有 FBX 统一导出到：
```
C:/EpicWkspc/LyraStarterGame/blender/fbx/
```

脚本中固定写法：
```python
base_path = "C:/EpicWkspc/LyraStarterGame/blender/fbx"
# 静态网格：f"{base_path}/SM_ModelName.fbx"
# 骨骼网格：f"{base_path}/SK_ModelName.fbx"
```

UE5 从该目录导入，不直接导出到 Content 目录（避免 Blender 覆盖 UE5 已处理的资产）。

---

### 单位说明

| | 单位 |
|--|------|
| Blender 默认 | 米（m） |
| UE5 | 厘米（cm） |

### ❌ 禁止规则：导出时不得乘以 100

```python
# ❌ 错误写法 — 禁止使用
global_scale=100.0,
apply_scale_options='FBX_SCALE_NONE',
```

原因：UE5.3+ Interchange 会自动读取 FBX 文件头的 `UnitScaleFactor=100` 再乘一次，
造成 **100 × 100 = 10000 倍**，模型极大。

### ✅ 正确规则：用 FBX_SCALE_UNITS 烘焙单位

```python
# ✅ 正确写法 — 所有 UE5 版本通用
global_scale=1.0,
apply_scale_options='FBX_SCALE_UNITS',
```

`FBX_SCALE_UNITS` 在导出时把 m→cm 换算烘焙进 FBX 几何体坐标，
FBX 文件内部声明单位为 cm，UE5 直接读取 cm 值，不再二次换算。

| 方案 | FBX 几何值（原 1.2m） | UE5 最终结果 |
|------|---|---|
| `scale=100` + `NONE` | 120，但头信息说×100 | **12000cm ❌ 极大** |
| `scale=1.0` + `UNITS` | 120，头信息说×1（已是cm） | **120cm ✅** |

### 静态网格

```python
bpy.ops.export_scene.fbx(
    filepath="C:/EpicWkspc/LyraStarterGame/blender/fbx/SM_Model.fbx",
    use_selection=True,
    global_scale=1.0,
    apply_scale_options='FBX_SCALE_UNITS',  # 将 m→cm 烘焙进几何体
    axis_forward='-Z',
    axis_up='Y',
    object_types={'MESH'},
    bake_anim=False,
    path_mode='AUTO'
)
```

### 骨骼网格（含动画）

```python
bpy.ops.export_scene.fbx(
    filepath="C:/EpicWkspc/LyraStarterGame/blender/fbx/SK_Model.fbx",
    use_selection=True,
    global_scale=1.0,
    apply_scale_options='FBX_SCALE_UNITS',  # 将 m→cm 烘焙进几何体
    axis_forward='-Z',
    axis_up='Y',
    object_types={'ARMATURE', 'MESH'},
    use_mesh_modifiers=True,          # 确保 Armature 修改器的蒙皮权重写入 FBX
    add_leaf_bones=False,
    bake_anim=True,
    bake_anim_use_all_actions=True,   # 配合开头清空 bpy.data.actions 使用
    bake_anim_step=1.0,
    bake_anim_simplify_factor=0.0,
    path_mode='AUTO'
)
```

### UE5 导入设置

| FBX 类型 | Import As | Import Animations | 其他 |
|---------|-----------|------------------|------|
| 静态网格 | Static Mesh | ❌ | 默认即可 |
| 骨骼网格 | Skeletal Mesh | ✅ | 默认即可，无需调整单位选项 |

---

## 七、常见错误速查

| 错误信息 | 原因 | 解决方式 |
|---------|------|---------|
| `KeyError: "Principled BSDF" not found` | UI 语言为中文，节点名已本地化 | 改用 `node.type == 'BSDF_PRINCIPLED'` |
| `TypeError: bpy_struct: item.attr = val` | 用数字索引访问了错误插槽 | 改用 `inputs.get("插槽名")` |
| `AttributeError: blend_method` | Blender 4.2+ 已移除该属性 | 用 `try/except AttributeError: pass` |
| `AttributeError: Action has no fcurves` | Blender 4.4+ Layered Action 结构变更 | 使用 `get_fcurves()` 兼容函数 |
| `RuntimeError: poll() context is incorrect` | 从文本编辑器运行，上下文非 3D 视口 | 使用 `temp_override` 注入视口上下文 |
| `ReferenceError: StructRNA removed` | `join()` 后访问了已合并的旧对象引用 | 先合并静态体，再创建动态部件 |
| 报错行号与代码不符 | 运行的是 .blend 内嵌旧版本 | 在文本编辑器全选粘贴最新代码 |
| UE5 导入出现大量中文动画片段 | 导出了所有历史 Action，Blender 中文 UI 自动生成中文名 | 脚本开头清空 `bpy.data.actions`，配合 `bake_anim_use_all_actions=True` 使用；`False` 会导致无动画导出 |
| 导出内容含中文名 | Blender 中文 UI 对 mesh data / action 自动生成中文名 | 合并后显式设置 `obj.data.name` 和 `action.name` 为英文 |
| `AttributeError: 'NoneType' has no attribute 'action'` | 新建 Armature 后 `animation_data` 默认为 `None`，不能直接赋值 | 在任何 `armature.animation_data.action =` 前先调用 `armature.animation_data_create()` |
| UE3 导入 PSA 提示"找不到 Root 轨迹" | 骨架缺少 Root 根骨骼，UE3 要求所有骨架必须有 Root | Armature 编辑时先建 Root 骨骼，其他骨骼设为 Root 的子骨骼 |

---

## 八、注意事项

- 脚本开头清场会删除所有对象，运行前先保存 `.blend` 文件
- 修改外部 `.py` 后，blend 内嵌版本不会自动同步，需手动粘贴
- `.blend\script.py.002` 这类路径表示 blend 内第三份缓存拷贝，与外部文件无关
