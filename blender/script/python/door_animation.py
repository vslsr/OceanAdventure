import bpy
import math

# 清空场景
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete()

# 动画设置
scene = bpy.context.scene
scene.render.fps = 24
scene.frame_start = 1
scene.frame_end = 48

# ── 材质工具函数 ──
def get_fcurves(action):
    if hasattr(action, 'fcurves'):
        return list(action.fcurves)
    fcurves = []
    if hasattr(action, 'layers'):
        for layer in action.layers:
            for strip in layer.strips:
                if hasattr(strip, 'channelbags'):
                    for bag in strip.channelbags:
                        fcurves.extend(bag.fcurves)
    return fcurves

def get_bsdf(mat):
    for node in mat.node_tree.nodes:
        if node.type == 'BSDF_PRINCIPLED':
            return node
    return None

def make_material(name, color):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    bsdf = get_bsdf(mat)
    if bsdf is None:
        return mat
    base = bsdf.inputs.get("Base Color") or bsdf.inputs.get("Color")
    if base:
        base.default_value = (*color, 1.0)
    return mat

mat_door  = make_material("Door",  (0.55, 0.35, 0.20))
mat_frame = make_material("Frame", (0.25, 0.15, 0.08))
mat_knob  = make_material("Knob",  (0.80, 0.65, 0.20))
mat_hinge = make_material("Hinge", (0.40, 0.40, 0.40))

# ── 尺寸参数 ──
dw = 1.0    # 门宽
dh = 2.1    # 门高
dt = 0.08   # 门厚
fw = 0.10   # 门框宽
ft = 0.12   # 门框厚度（Y 方向）

# 铰链轴设在 x = 0，门向 x 正方向延伸

# ── 门框（静态，不参与动画）──
# 左侧门框（铰链侧）
bpy.ops.mesh.primitive_cube_add(location=(-fw / 2, 0, dh / 2))
obj = bpy.context.object
obj.name = "Frame_Left"
obj.scale = (fw / 2, ft / 2, (dh + fw) / 2)
obj.data.materials.append(mat_frame)

# 右侧门框
bpy.ops.mesh.primitive_cube_add(location=(dw + fw / 2, 0, dh / 2))
obj = bpy.context.object
obj.name = "Frame_Right"
obj.scale = (fw / 2, ft / 2, (dh + fw) / 2)
obj.data.materials.append(mat_frame)

# 上横梁
bpy.ops.mesh.primitive_cube_add(location=(dw / 2, 0, dh + fw / 2))
obj = bpy.context.object
obj.name = "Frame_Top"
obj.scale = (dw / 2 + fw, ft / 2, fw / 2)
obj.data.materials.append(mat_frame)

# ── 合页（铰链装饰）──
for z in (dh * 0.15, dh * 0.50, dh * 0.85):
    bpy.ops.mesh.primitive_cube_add(location=(0, dt / 2 + 0.01, z))
    h = bpy.context.object
    h.name = f"Hinge_{z:.2f}"
    h.scale = (0.03, 0.02, 0.06)
    h.data.materials.append(mat_hinge)

# ── 铰链空物体（旋转轴心）──
bpy.ops.object.empty_add(type='PLAIN_AXES', location=(0, 0, 0))
hinge = bpy.context.object
hinge.name = "Door_Hinge"

# ── 门板：左边缘对齐铰链轴 x=0，中心在 (dw/2, 0, dh/2) ──
bpy.ops.mesh.primitive_cube_add(location=(dw / 2, 0, dh / 2))
door_panel = bpy.context.object
door_panel.name = "Door_Panel"
door_panel.scale = (dw / 2, dt / 2, dh / 2)
door_panel.data.materials.append(mat_door)

# ── 门把手（杆）──
bpy.ops.mesh.primitive_cylinder_add(
    radius=0.025, depth=0.12,
    location=(dw - 0.15, dt / 2 + 0.06, dh * 0.45)
)
knob_bar = bpy.context.object
knob_bar.name = "Knob_Bar"
knob_bar.rotation_euler[1] = math.pi / 2
knob_bar.data.materials.append(mat_knob)

# ── 门把手（球头）──
bpy.ops.mesh.primitive_uv_sphere_add(
    radius=0.04,
    location=(dw - 0.15, dt / 2 + 0.12, dh * 0.45)
)
knob_ball = bpy.context.object
knob_ball.name = "Knob_Ball"
knob_ball.data.materials.append(mat_knob)

# ── 将门板和把手绑定到铰链空物体 ──
bpy.ops.object.select_all(action='DESELECT')
door_panel.select_set(True)
knob_bar.select_set(True)
knob_ball.select_set(True)
hinge.select_set(True)
bpy.context.view_layer.objects.active = hinge
bpy.ops.object.parent_set(type='OBJECT', keep_transform=True)

# ── 开门动画 ──
# 第 1 帧：关门（0°）
# 第 30 帧：开门（90°，向外旋转）
hinge.rotation_mode = 'XYZ'

scene.frame_set(1)
hinge.rotation_euler.z = 0
hinge.keyframe_insert(data_path="rotation_euler", frame=1)

scene.frame_set(30)
hinge.rotation_euler.z = math.radians(90)
hinge.keyframe_insert(data_path="rotation_euler", frame=30)

# 平滑插值（缓入缓出）
if hinge.animation_data and hinge.animation_data.action:
    for fc in get_fcurves(hinge.animation_data.action):
        for kp in fc.keyframe_points:
            kp.interpolation = 'BEZIER'

scene.frame_set(1)

print("完成：门建模 + 开门动画")
print("  第  1 帧：关门（0°）")
print("  第 30 帧：开门（90°）")
print("按空格键预览动画")
