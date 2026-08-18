"""
锅炉骨骼动画导出脚本（UE5 兼容版）
修复：自动注入 3D 视口上下文，解决 context is incorrect 错误
"""
import bpy
import math

# ── 清空场景和历史数据（直接 API，无需视口上下文）──
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)

# 清空所有旧 Action（防止历史运行残留中文 Action 被一起导出）
for action in list(bpy.data.actions):
    bpy.data.actions.remove(action)

# 清空孤立材质（防止重复运行产生重名材质）
for mat in list(bpy.data.materials):
    if mat.users == 0:
        bpy.data.materials.remove(mat)

# ── 材质工具 ──
def get_fcurves(action):
    """兼容 Blender 3.x / 4.0-4.3（legacy）和 4.4+（layered action）"""
    if hasattr(action, 'fcurves'):          # 3.x / 4.0-4.3
        return list(action.fcurves)
    fcurves = []                            # 4.4+ layered action
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

# ── 所有建模逻辑放入 main()──
def main():
    scene = bpy.context.scene
    scene.render.fps = 24
    scene.frame_start = 1
    scene.frame_end = 24

    mat_body   = make_material("Body",   (0.25, 0.28, 0.30), metallic=0.8, roughness=0.4)
    mat_lid    = make_material("Lid",    (0.30, 0.33, 0.35), metallic=0.8, roughness=0.3)
    mat_pipe   = make_material("Pipe",   (0.40, 0.38, 0.32), metallic=0.9, roughness=0.3)
    mat_handle = make_material("Handle", (0.15, 0.10, 0.08), metallic=0.0, roughness=0.9)
    mat_gauge  = make_material("Gauge",  (0.85, 0.80, 0.70), metallic=0.6, roughness=0.2)

    br = 0.36   # 锅体半径（原0.60，缩小40%）
    bh = 0.72   # 锅体高度（原1.20，缩小40%）
    lr = 0.39   # 盖子半径（原0.65，缩小40%）
    lh = 0.055  # 盖子厚度（原0.09，缩小40%）
    s  = 0.6    # 统一缩放因子，所有硬编码细节尺寸乘以此值

    # ── STEP 1：静态部件 ──
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=br, depth=bh, location=(0, 0, bh/2))
    bpy.context.object.name = "Boiler_Body"
    bpy.context.object.data.materials.append(mat_body)

    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=br+0.06*s, depth=0.06*s, location=(0, 0, 0.03*s))
    bpy.context.object.name = "Boiler_Base"
    bpy.context.object.data.materials.append(mat_body)

    for i, (lx, ly) in enumerate([(0.35*s, 0.35*s),(-0.35*s, 0.35*s),(0.35*s,-0.35*s),(-0.35*s,-0.35*s)]):
        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.055*s, depth=0.28*s, location=(lx, ly, -0.14*s))
        bpy.context.object.name = f"Leg_{i+1}"
        bpy.context.object.data.materials.append(mat_body)

    for z in (bh*0.25, bh*0.50, bh*0.75):
        bpy.ops.mesh.primitive_torus_add(major_radius=br+0.025*s, minor_radius=0.022*s, location=(0, 0, z))
        bpy.context.object.name = f"Ring_{z:.2f}"
        bpy.context.object.data.materials.append(mat_body)

    bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.06*s, depth=0.5*s,
                                         location=(br*0.65, br*0.55, bh+0.25*s))
    bpy.context.object.name = "Steam_Pipe"
    bpy.context.object.data.materials.append(mat_pipe)

    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.09*s, depth=0.05*s,
                                         location=(br+0.02*s, 0, bh*0.65))
    gauge = bpy.context.object
    gauge.name = "Pressure_Gauge"
    gauge.rotation_euler[1] = math.pi / 2
    gauge.data.materials.append(mat_gauge)

    bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.04*s, depth=0.18*s,
                                         location=(-br-0.06*s, 0, bh*0.35))
    valve = bpy.context.object
    valve.name = "Valve"
    valve.rotation_euler[1] = math.pi / 2
    valve.data.materials.append(mat_pipe)

    # ── STEP 2：合并静态部件（盖子创建前）──
    static_parts = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    bpy.ops.object.select_all(action='DESELECT')
    for o in static_parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = static_parts[0]
    bpy.ops.object.join()
    boiler_static = bpy.context.object
    boiler_static.name      = "SM_Boiler_Body"
    boiler_static.data.name = "SM_Boiler_Body_Mesh"   # 网格数据名也设为英文

    # UV 展开（ActorX / UE3 导出 PSK 必须有 UV Map）
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.02)
    bpy.ops.object.mode_set(mode='OBJECT')
    print("OK: SM_Boiler_Body 合并完成")

    # ── STEP 3：盖子部件 ──
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=lr, depth=lh, location=(0, 0, bh+lh/2))
    lid = bpy.context.object
    lid.name = "Lid"
    lid.data.materials.append(mat_lid)

    bpy.ops.mesh.primitive_torus_add(major_radius=lr, minor_radius=0.025*s, location=(0, 0, bh))
    lid_rim = bpy.context.object
    lid_rim.name = "Lid_Rim"
    lid_rim.data.materials.append(mat_lid)

    bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.028*s, depth=0.07*s,
                                         location=(0, 0, bh+lh+0.035*s))
    handle_base = bpy.context.object
    handle_base.name = "Lid_Handle_Base"
    handle_base.data.materials.append(mat_handle)

    bpy.ops.mesh.primitive_torus_add(major_radius=0.09*s, minor_radius=0.025*s,
                                      location=(0, 0, bh+lh+0.07*s))
    handle_ring = bpy.context.object
    handle_ring.name = "Lid_Handle_Ring"
    handle_ring.data.materials.append(mat_handle)

    # 盖子合并为单一网格
    bpy.ops.object.select_all(action='DESELECT')
    for o in (lid, lid_rim, handle_base, handle_ring):
        o.select_set(True)
    bpy.context.view_layer.objects.active = lid
    bpy.ops.object.join()
    lid_combined = bpy.context.object
    lid_combined.name      = "SK_Boiler_Lid"
    lid_combined.data.name = "SK_Boiler_Lid_Mesh"     # 网格数据名也设为英文

    # UV 展开（ActorX / UE3 导出 PSK 必须有 UV Map）
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.02)
    bpy.ops.object.mode_set(mode='OBJECT')
    print("OK: SK_Boiler_Lid 合并完成")

    # ── STEP 4：Armature ──
    # UE3 要求骨架必须有 Root 根骨骼，Lid_Bone 作为子骨骼
    bpy.ops.object.armature_add(location=(0, 0, 0))
    armature = bpy.context.object
    # Armature 对象本身命名为 Root，避免 FBX 多出一层 Dummy 节点
    armature.name = "Boiler_Armature"
    armature.data.name = "Boiler_Bones"
    bpy.ops.object.mode_set(mode='EDIT')

    # Root 骨骼（根节点，无动画）
    root_bone = armature.data.edit_bones[0]
    root_bone.name = "Root"
    root_bone.head = (0.0, 0.0, 0.0)
    root_bone.tail = (0.0, 0.0, 0.1)

    # Lid_Bone：铰链轴在盖子后边缘，作为 Root 的子骨骼
    lid_bone_eb = armature.data.edit_bones.new(name="Lid_Bone")
    lid_bone_eb.head = (0.0, -lr, bh)
    lid_bone_eb.tail = (0.0, -lr, bh + 0.3)
    lid_bone_eb.parent = root_bone

    bpy.ops.object.mode_set(mode='OBJECT')

    # ── STEP 5：骨骼绑定（直接 Python 赋值，不走 bpy.ops）──
    # 设置父子关系
    lid_combined.parent = armature
    lid_combined.parent_type = 'OBJECT'
    lid_combined.matrix_parent_inverse = armature.matrix_world.inverted()

    # 手动添加 Armature 修改器并明确指向骨骼
    mod = lid_combined.modifiers.new(name="Armature", type='ARMATURE')
    mod.object = armature
    mod.use_vertex_groups = True

    # 所有顶点全权重绑定到 Lid_Bone（刚体，不需要权重绘制）
    vg = lid_combined.vertex_groups.new(name='Lid_Bone')
    vg.add(list(range(len(lid_combined.data.vertices))), 1.0, 'REPLACE')
    print("OK: 骨骼绑定完成")

    # ── STEP 6：动画 ──
    bpy.ops.object.select_all(action='DESELECT')
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.mode_set(mode='POSE')
    pose_bone = armature.pose.bones["Lid_Bone"]
    pose_bone.rotation_mode = 'XYZ'

    def add_keyframe(frame, angle_deg):
        scene.frame_set(frame)
        pose_bone.rotation_euler.x = math.radians(angle_deg)
        pose_bone.keyframe_insert(data_path="rotation_euler", frame=frame)

    def set_interpolation(action, mode='BEZIER'):
        for fc in get_fcurves(action):
            for kp in fc.keyframe_points:
                kp.interpolation = mode

    # 确保 animation_data 已初始化（直接赋值前必须先 create）
    armature.animation_data_create()

    # ── Action 1: Idle（盖子保持打开，什么都不做）──
    idle_action = bpy.data.actions.new(name="Boiler_Lid_Idle")
    armature.animation_data.action = idle_action
    add_keyframe(1,  110)
    add_keyframe(24, 110)
    set_interpolation(idle_action, 'CONSTANT')  # 静止状态用 CONSTANT，无过渡
    print("OK: Action Boiler_Lid_Idle 创建完成")

    # ── Action 2: Close（盖子从打开缓缓关闭）──
    close_action = bpy.data.actions.new(name="Boiler_Lid_Close")
    armature.animation_data.action = close_action
    add_keyframe(1,  110)
    add_keyframe(24,   0)
    set_interpolation(close_action, 'BEZIER')   # 关闭动作用 BEZIER 缓入缓出
    print("OK: Action Boiler_Lid_Close 创建完成")

    bpy.ops.object.mode_set(mode='OBJECT')
    scene.frame_set(1)

    # ── STEP 7：导出 FBX ──
    base_path = "C:/EpicWkspc/LyraStarterGame/blender/fbx"

    bpy.ops.object.select_all(action='DESELECT')
    boiler_static.select_set(True)
    bpy.context.view_layer.objects.active = boiler_static
    bpy.ops.export_scene.fbx(
        filepath=f"{base_path}/SM_Boiler_Body.fbx",
        use_selection=True,
        global_scale=1.0,
        apply_scale_options='FBX_SCALE_UNITS',  # 将 m→cm 烘焙进几何体，UE5 不再二次换算
        axis_forward='-Z', axis_up='Y',
        object_types={'MESH'}, bake_anim=False, path_mode='AUTO'
    )
    print("导出完成：SM_Boiler_Body.fbx")

    bpy.ops.object.select_all(action='DESELECT')
    lid_combined.select_set(True)
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.export_scene.fbx(
        filepath=f"{base_path}/SK_Boiler_Lid.fbx",
        use_selection=True,
        global_scale=1.0,
        apply_scale_options='FBX_SCALE_UNITS',  # 将 m→cm 烘焙进几何体，UE5 不再二次换算
        axis_forward='-Z', axis_up='Y',
        object_types={'ARMATURE', 'MESH'},
        use_mesh_modifiers=True,          # 确保 Armature 修改器的蒙皮权重写入 FBX
        add_leaf_bones=False, bake_anim=True,
        bake_anim_use_all_actions=True,   # 开头已清空旧 Action，此时只有 Boiler_Lid_Idle + Boiler_Lid_Close
        bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
        path_mode='AUTO'
    )
    print("导出完成：SK_Boiler_Lid.fbx")
    print("全部完成！")

# ── 查找 3D 视口并注入上下文执行 main() ──
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
