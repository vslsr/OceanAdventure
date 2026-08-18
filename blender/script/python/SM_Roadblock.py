# SM_Roadblock —— A 字斜撑路障（拒马 / sawhorse barricade）
# 结构：两端各一个 A 字支腿（apex 在顶、双脚向 ±Y 外撑），
#       中间由若干横杆（rail）沿 X 跨接，越靠下越外撑、越靠上越收拢，
#       形成图片里的 # / 梯形外观；顶部加一根脊梁连接两 apex。
# 尺寸（UE5 cm）：W(X)=200  H(Z)=100  D(Y)=70
# Placement   : can_ground=True
# 先不创建动画（纯静态网格）。

import bpy
import math
from mathutils import Vector

# ── 清场（直接 API，不走 bpy.ops，无需视口上下文）────────────────────────
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)
for mat in list(bpy.data.materials):
    if mat.users == 0:
        bpy.data.materials.remove(mat)

# ── 材质工具函数 ─────────────────────────────────────────────────────────
def get_bsdf(mat):
    for node in mat.node_tree.nodes:
        if node.type == 'BSDF_PRINCIPLED':
            return node
    return None

def make_material(name, color, metallic=0.0, roughness=0.6, alpha=1.0):
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
            mat.blend_method = 'BLEND'
        except AttributeError:
            pass
    return mat

# ── 尺寸参数（米）────────────────────────────────────────────────────────
SPAN   = 2.00    # X 总宽（横杆长度）        → 200 cm
H      = 1.00    # Z 高度（apex 到地面）      → 100 cm
SPREAD = 0.35    # Y 单侧外撑（脚到中线）     → 总深 70 cm
T      = 0.09    # 梁厚（方截面边长）         → 9 cm

FRAME_X = SPAN * 0.40            # A 字支腿所在 X（让横杆两端略微外伸）
RAIL_FRACS = (0.10, 0.42, 0.74)  # 横杆高度占 H 的比例（下宽上窄）

# 竖条：沿宽度均布的若干根，落在与横杆相同的斜面上，纵跨整高、横切每根横杆
VERT_XF   = (-0.85, -0.45, 0.0, 0.45, 0.85)  # 竖条 X 位置（× SPAN/2）
VERT_FLOW = 0.06                 # 竖条下端高度占 H 比例
VERT_FTOP = 0.98                 # 竖条上端高度占 H 比例

# ── 通用方梁：在 p1→p2 之间生成一根方截面梁 ──────────────────────────────
def make_beam(name, p1, p2, t, mat):
    p1 = Vector(p1)
    p2 = Vector(p2)
    d = p2 - p1
    length = d.length
    bpy.ops.mesh.primitive_cube_add(location=(0.0, 0.0, 0.0))
    obj = bpy.context.object
    obj.name = name
    obj.scale = (length / 2.0, t / 2.0, t / 2.0)   # cube 默认 ±1，故 /2
    obj.rotation_mode = 'QUATERNION'
    obj.rotation_quaternion = d.to_track_quat('X', 'Z')  # 局部 X 对齐梁方向
    obj.location = (p1 + p2) / 2.0
    obj.data.materials.append(mat)
    return obj

# ── 建模 ─────────────────────────────────────────────────────────────────
def main():
    mat = make_material("M_Roadblock", (0.88, 0.84, 0.22),
                        metallic=0.0, roughness=0.55)

    parts = []

    # 两端 A 字支腿：apex 在顶中线，双脚向 ±Y 外撑落地
    for sx in (-1, 1):
        fx = sx * FRAME_X
        apex = (fx, 0.0, H)
        foot_p = (fx,  SPREAD, 0.0)
        foot_n = (fx, -SPREAD, 0.0)
        parts.append(make_beam(f"Leg_{sx}_P", apex, foot_p, T, mat))
        parts.append(make_beam(f"Leg_{sx}_N", apex, foot_n, T, mat))

    # 横杆：每个高度在 ±Y 各一根，沿 X 全宽跨接（贴着支腿外缘）
    for i, f in enumerate(RAIL_FRACS):
        z = f * H
        ry = SPREAD * (1.0 - f)          # 支腿在该高度处的 Y，使横杆贴腿
        for sy in (-1, 1):
            y = sy * ry
            parts.append(make_beam(
                f"Rail_{i}_{sy}",
                (-SPAN / 2.0, y, z), (SPAN / 2.0, y, z), T, mat))

    # 竖条：落在斜面 Y=SPREAD*(1-z/H) 上，下宽上窄，横切每根横杆形成 # 网格
    for j, xf in enumerate(VERT_XF):
        x = xf * SPAN / 2.0
        for sy in (-1, 1):
            p1 = (x, sy * SPREAD * (1.0 - VERT_FLOW), VERT_FLOW * H)
            p2 = (x, sy * SPREAD * (1.0 - VERT_FTOP), VERT_FTOP * H)
            parts.append(make_beam(f"Vert_{j}_{sy}", p1, p2, T, mat))

    # 顶部脊梁：连接两端 apex，沿 X 全宽
    parts.append(make_beam("Ridge",
                          (-SPAN / 2.0, 0.0, H), (SPAN / 2.0, 0.0, H), T, mat))

    # ── 合并为单一静态网格 ──
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    sm = bpy.context.object
    sm.name = "SM_Roadblock"
    sm.data.name = "SM_Roadblock_Mesh"

    # 应用变换（导出前必须烘焙旋转/缩放/位移进顶点坐标）
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    # UV 展开
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.02)
    bpy.ops.object.mode_set(mode='OBJECT')

    # FBX 导出
    # ⚠️ 例外：本模型临时需要 ×100（仅此模型，不是 claude-blender.md 规范）。
    #    global_scale=100 + FBX_SCALE_UNITS = 干净的 ×100；
    #    切勿改成 scale=100 + FBX_SCALE_NONE（会被表头二次相乘变成 10000×）。
    export_path = "C:/EpicWkspc/LyraStarterGame/blender/fbx/SM_Roadblock.fbx"
    bpy.ops.object.select_all(action='DESELECT')
    sm.select_set(True)
    bpy.context.view_layer.objects.active = sm
    bpy.ops.export_scene.fbx(
        filepath=export_path,
        use_selection=True,
        global_scale=100.0,                       # 本模型临时 ×100（例外，非规范）
        apply_scale_options='FBX_SCALE_UNITS',    # 仍用 UNITS，避免表头二次相乘
        axis_forward='-Z',
        axis_up='Y',
        object_types={'MESH'},
        bake_anim=False,
        path_mode='AUTO',
    )

    print(f"SM_Roadblock 导出完成: {export_path}")
    print(f"  尺寸（UE5 cm，已×100）: W=20000  H=10000  D=7000")


# ── 注入 3D 视口上下文（解决 poll() context is incorrect）────────────────
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
