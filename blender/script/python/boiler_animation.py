import bpy
import math

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete()

scene = bpy.context.scene
scene.render.fps = 24
scene.frame_start = 1
scene.frame_end = 24   # 打开(1) → 关闭(24)

# ── 材质工具 ──
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

def make_material(name, color, metallic=0.0, roughness=0.5):
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
    return mat

mat_body   = make_material("Body",   (0.25, 0.28, 0.30), metallic=0.8, roughness=0.4)
mat_lid    = make_material("Lid",    (0.30, 0.33, 0.35), metallic=0.8, roughness=0.3)
mat_pipe   = make_material("Pipe",   (0.40, 0.38, 0.32), metallic=0.9, roughness=0.3)
mat_handle = make_material("Handle", (0.15, 0.10, 0.08), metallic=0.0, roughness=0.9)
mat_gauge  = make_material("Gauge",  (0.85, 0.80, 0.70), metallic=0.6, roughness=0.2)

# ── 尺寸参数 ──
br = 0.60   # 锅体半径
bh = 1.20   # 锅体高度
lr = 0.65   # 盖子半径
lh = 0.09   # 盖子厚度

# ── 锅炉主体 ──
bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=br, depth=bh,
                                     location=(0, 0, bh / 2))
body = bpy.context.object
body.name = "Boiler_Body"
body.data.materials.append(mat_body)

# 底座
bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=br + 0.06, depth=0.06,
                                     location=(0, 0, 0.03))
bpy.context.object.name = "Boiler_Base"
bpy.context.object.data.materials.append(mat_body)

# 4 条腿
for i, (lx, ly) in enumerate([(0.35, 0.35), (-0.35, 0.35), (0.35, -0.35), (-0.35, -0.35)]):
    bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.055, depth=0.28,
                                         location=(lx, ly, -0.14))
    bpy.context.object.name = f"Leg_{i + 1}"
    bpy.context.object.data.materials.append(mat_body)

# 身体加强箍（装饰环）
for z in (bh * 0.25, bh * 0.50, bh * 0.75):
    bpy.ops.mesh.primitive_torus_add(
        major_radius=br + 0.025, minor_radius=0.022,
        location=(0, 0, z)
    )
    bpy.context.object.name = f"Ring_{z:.2f}"
    bpy.context.object.data.materials.append(mat_body)

# 蒸汽管（前右侧，不遮挡盖子开合方向）
bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.06, depth=0.5,
                                     location=(br * 0.65, br * 0.55, bh + 0.25))
bpy.context.object.name = "Steam_Pipe"
bpy.context.object.data.materials.append(mat_pipe)

bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.09, depth=0.04,
                                     location=(br * 0.65, br * 0.55, bh + 0.52))
bpy.context.object.name = "Pipe_Cap"
bpy.context.object.data.materials.append(mat_pipe)

# 压力表（右侧）
bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.09, depth=0.05,
                                     location=(br + 0.02, 0, bh * 0.65))
gauge = bpy.context.object
gauge.name = "Pressure_Gauge"
gauge.rotation_euler[1] = math.pi / 2
gauge.data.materials.append(mat_gauge)

# 表盘面
bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.07, depth=0.01,
                                     location=(br + 0.055, 0, bh * 0.65))
face = bpy.context.object
face.name = "Gauge_Face"
face.rotation_euler[1] = math.pi / 2
face.data.materials.append(make_material("GaugeFace", (0.95, 0.95, 0.90)))

# 阀门（左侧）
bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.04, depth=0.18,
                                     location=(-br - 0.06, 0, bh * 0.35))
valve = bpy.context.object
valve.name = "Valve"
valve.rotation_euler[1] = math.pi / 2
valve.data.materials.append(mat_pipe)

bpy.ops.mesh.primitive_torus_add(major_radius=0.06, minor_radius=0.015,
                                   location=(-br - 0.16, 0, bh * 0.35))
wheel = bpy.context.object
wheel.name = "Valve_Wheel"
wheel.rotation_euler[1] = math.pi / 2
wheel.data.materials.append(mat_pipe)

# ── 合并所有静态部件为一个网格（盖子创建前执行）──
static_parts = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
bpy.ops.object.select_all(action='DESELECT')
for obj in static_parts:
    obj.select_set(True)
bpy.context.view_layer.objects.active = static_parts[0]
bpy.ops.object.join()
boiler_static = bpy.context.object
boiler_static.name = "Boiler_Static"

# ── 铰链空物体（轴心：盖子后边缘，y = -lr，盖子向后翻开）──
bpy.ops.object.empty_add(type='PLAIN_AXES', location=(0, -lr, bh))
lid_hinge = bpy.context.object
lid_hinge.name = "Lid_Hinge"

# ── 盖子（圆柱）──
# 中心在 (0, 0, bh+lh/2)，后边缘正好对齐铰链 y=-lr
bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=lr, depth=lh,
                                     location=(0, 0, bh + lh / 2))
lid = bpy.context.object
lid.name = "Lid"
lid.data.materials.append(mat_lid)

# 盖子外沿
bpy.ops.mesh.primitive_torus_add(major_radius=lr, minor_radius=0.025,
                                   location=(0, 0, bh))
lid_rim = bpy.context.object
lid_rim.name = "Lid_Rim"
lid_rim.data.materials.append(mat_lid)

# 盖子把手底座
bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.028, depth=0.07,
                                     location=(0, 0, bh + lh + 0.035))
handle_base = bpy.context.object
handle_base.name = "Lid_Handle_Base"
handle_base.data.materials.append(mat_handle)

# 盖子把手圆环
bpy.ops.mesh.primitive_torus_add(major_radius=0.09, minor_radius=0.025,
                                   location=(0, 0, bh + lh + 0.07))
handle_ring = bpy.context.object
handle_ring.name = "Lid_Handle_Ring"
handle_ring.data.materials.append(mat_handle)

# ── 将盖子所有部件绑定到铰链 ──
bpy.ops.object.select_all(action='DESELECT')
for obj in (lid, lid_rim, handle_base, handle_ring):
    obj.select_set(True)
lid_hinge.select_set(True)
bpy.context.view_layer.objects.active = lid_hinge
bpy.ops.object.parent_set(type='OBJECT', keep_transform=True)

# ── 开关动画 ──
lid_hinge.rotation_mode = 'XYZ'

def keyframe(frame, angle_deg):
    scene.frame_set(frame)
    lid_hinge.rotation_euler.x = math.radians(angle_deg)
    lid_hinge.keyframe_insert(data_path="rotation_euler", frame=frame)

keyframe(1,  110)   # 初始状态：盖子打开 110°
keyframe(24,   0)   # 缓缓关闭

# 平滑缓入缓出
if lid_hinge.animation_data and lid_hinge.animation_data.action:
    for fc in get_fcurves(lid_hinge.animation_data.action):
        for kp in fc.keyframe_points:
            kp.interpolation = 'BEZIER'

scene.frame_set(1)

print("完成：锅炉建模 + 盖子开关动画")
print("  第  1 帧：盖子打开（110°，初始状态）")
print("  第 24 帧：盖子关闭（0°）")
print("按空格键预览动画")
