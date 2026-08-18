import bpy

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete()

def get_bsdf(mat):
    for node in mat.node_tree.nodes:
        if node.type == 'BSDF_PRINCIPLED':
            return node
    return None

def make_material(name, color, alpha=1.0):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    bsdf = get_bsdf(mat)
    if bsdf is None:
        return mat
    # 按名称访问插槽，兼容 Blender 3.x / 4.x
    base = bsdf.inputs.get("Base Color") or bsdf.inputs.get("Color")
    if base:
        base.default_value = (color[0], color[1], color[2], 1.0)
    if alpha < 1.0:
        a_socket = bsdf.inputs.get("Alpha")
        if a_socket:
            a_socket.default_value = alpha
        # blend_method 在 4.2+ 已移除，用 try/except 兼容
        try:
            mat.blend_method = 'BLEND'
        except AttributeError:
            pass
    return mat

mat_wall  = make_material("Wall",  (0.8,  0.75, 0.65))
mat_glass = make_material("Glass", (0.5,  0.80, 1.00), alpha=0.2)
mat_door  = make_material("Door",  (0.55, 0.35, 0.20))
mat_frame = make_material("Frame", (0.25, 0.15, 0.08))
mat_knob  = make_material("Knob",  (0.8,  0.65, 0.20))

# 主体建筑
bpy.ops.mesh.primitive_cube_add(location=(0, 0, 3))
building = bpy.context.object
building.name = "Building_Main"
building.scale = (4, 3, 3)
building.data.materials.append(mat_wall)

# 窗户（三层，每层三个）
for floor in range(3):
    z = 1.0 + floor * 2.0
    for x in (-1.5, 0.0, 1.5):
        bpy.ops.mesh.primitive_cube_add(location=(x, 3.05, z))
        win = bpy.context.object
        win.name = f"Window_F{floor+1}"
        win.scale = (0.4, 0.05, 0.5)
        win.data.materials.append(mat_glass)

# 门框尺寸参数
fw = 0.08
fd = 0.06
dw = 0.55
dh = 1.0
dy = 3.05

# 门板
bpy.ops.mesh.primitive_cube_add(location=(0, dy, dh / 2))
door_panel = bpy.context.object
door_panel.name = "Door_Panel"
door_panel.scale = (dw / 2, fd / 2, dh / 2)
door_panel.data.materials.append(mat_door)

# 左门框
bpy.ops.mesh.primitive_cube_add(location=(-(dw / 2 + fw / 2), dy, dh / 2))
frame_l = bpy.context.object
frame_l.name = "DoorFrame_Left"
frame_l.scale = (fw / 2, fd / 2, dh / 2)
frame_l.data.materials.append(mat_frame)

# 右门框
bpy.ops.mesh.primitive_cube_add(location=((dw / 2 + fw / 2), dy, dh / 2))
frame_r = bpy.context.object
frame_r.name = "DoorFrame_Right"
frame_r.scale = (fw / 2, fd / 2, dh / 2)
frame_r.data.materials.append(mat_frame)

# 上门框
bpy.ops.mesh.primitive_cube_add(location=(0, dy, dh + fw / 2))
frame_t = bpy.context.object
frame_t.name = "DoorFrame_Top"
frame_t.scale = (dw / 2 + fw, fd / 2, fw / 2)
frame_t.data.materials.append(mat_frame)

# 门把手杆
bpy.ops.mesh.primitive_cylinder_add(
    radius=0.03, depth=0.12,
    location=(dw / 2 - 0.08, dy + fd / 2 + 0.06, 0.55))
knob_bar = bpy.context.object
knob_bar.name = "DoorKnob_Bar"
knob_bar.rotation_euler[0] = 1.5708
knob_bar.data.materials.append(mat_knob)

# 门把手球头
bpy.ops.mesh.primitive_uv_sphere_add(
    radius=0.045,
    location=(dw / 2 - 0.08, dy + fd / 2 + 0.12, 0.55))
knob_ball = bpy.context.object
knob_ball.name = "DoorKnob_Ball"
knob_ball.data.materials.append(mat_knob)

# 屋顶
bpy.ops.mesh.primitive_cone_add(vertices=4, location=(0, 0, 7))
roof = bpy.context.object
roof.name = "Roof"
roof.scale = (4.5, 3.5, 1.5)
roof.rotation_euler[2] = 0.785
roof.data.materials.append(mat_wall)

print("建筑创建完成!")
