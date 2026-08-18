# SM_Car —— 小车静态网格（低模块面小汽车）
# 结构：底盘车身（box）+ 上部座舱（box，顶部四面收拢成梯形驾驶室）
#       + 前后保险杠（box）+ 四个车轮（cylinder，轴向沿 ±Y）。
# 尺寸（UE5 cm）：L(X)=430  W(Y)=185  H(Z)=150
# Placement   : can_ground=True（车轮底面贴 z=0）
# 材质        : 全车共用 1 个材质槽 M_Car_Flow（占位），
#               导入 UE5 后在该槽位挂流光材质即可，无需改模型。
# 先不创建动画（纯静态网格）。

import bpy
import math

# ── 清场（直接 API，不走 bpy.ops，无需视口上下文）────────────────────────
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)
for action in list(bpy.data.actions):
    bpy.data.actions.remove(action)
for mat in list(bpy.data.materials):
    if mat.users == 0:
        bpy.data.materials.remove(mat)

# ── 材质工具函数 ─────────────────────────────────────────────────────────
def get_bsdf(mat):
    for node in mat.node_tree.nodes:
        if node.type == 'BSDF_PRINCIPLED':
            return node
    return None

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
            mat.blend_method = 'BLEND'
        except AttributeError:
            pass
    return mat

# ── 尺寸参数（米）────────────────────────────────────────────────────────
BODY_L   = 4.10   # 车身长（X）              → 410 cm
BODY_W   = 1.60   # 车身宽（Y）              → 160 cm
BODY_H   = 0.55   # 车身高（Z）              → 55 cm
BODY_Z0  = 0.40   # 车身底面离地             → 40 cm

CABIN_L  = 2.00   # 座舱长（X）              → 200 cm
CABIN_W  = 1.50   # 座舱宽（Y）              → 150 cm
CABIN_H  = 0.55   # 座舱高（Z）              → 55 cm
CABIN_X  = -0.15  # 座舱前后偏移（负=偏后，留出车头引擎盖）
CABIN_TAPER = 0.62  # 座舱顶面收拢比例（做出前后风挡与侧窗倾角）

WHEEL_R  = 0.35   # 轮半径                   → 35 cm
WHEEL_W  = 0.25   # 轮宽（Y 向厚度）         → 25 cm
WHEEL_X  = 1.35   # 前后轴 X 位置（± 取轴距）
WHEEL_Y  = 0.80   # 左右轮 Y 位置（± 取轮距）
WHEEL_SEG = 24    # 轮圆周分段（低模）

BUMPER_L = 0.10   # 保险杠厚（X）
BUMPER_W = 1.65   # 保险杠宽（Y）
BUMPER_H = 0.30   # 保险杠高（Z）
BUMPER_X = 2.10   # 保险杠 X 位置（± 车头/车尾），外缘 = 2.15

# ── 通用方块：以 center 为中心生成 size=(x,y,z) 的方块 ───────────────────
def make_box(name, center, size, mat):
    bpy.ops.mesh.primitive_cube_add(location=center)
    obj = bpy.context.object
    obj.name = name
    obj.scale = (size[0] / 2.0, size[1] / 2.0, size[2] / 2.0)  # cube 默认 ±1，故 /2
    obj.data.materials.append(mat)
    return obj

# ── 车轮：cylinder 默认轴向为 Z，绕 X 转 90° 使轴向沿 Y ──────────────────
def make_wheel(name, center, mat):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=WHEEL_SEG,
        radius=WHEEL_R,
        depth=WHEEL_W,
        location=center,
        rotation=(math.radians(90.0), 0.0, 0.0),
    )
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.append(mat)
    return obj

# ── 顶面收拢：把局部坐标 z>0 的顶点向轴心收，做出驾驶室梯形轮廓 ──────────
#    对 primitive cube 有效（局部顶点恒为 ±1，与 obj.scale 无关）
def taper_top(obj, factor):
    for v in obj.data.vertices:
        if v.co.z > 0.0:
            v.co.x *= factor
            v.co.y *= factor

# ── 建模 ─────────────────────────────────────────────────────────────────
def main():
    # 全车唯一材质：占位色，UE5 导入后在此槽位换成流光材质
    mat = make_material("M_Car_Flow", (0.18, 0.42, 0.75),
                        metallic=0.3, roughness=0.45)

    parts = []

    # 车身（底盘）
    body_z = BODY_Z0 + BODY_H / 2.0                       # 中心 0.675，顶面 0.95
    parts.append(make_box("Body", (0.0, 0.0, body_z),
                          (BODY_L, BODY_W, BODY_H), mat))

    # 座舱：坐在车身顶面上，顶部四面收拢
    cabin_z = BODY_Z0 + BODY_H + CABIN_H / 2.0            # 中心 1.225，顶面 1.50
    cabin = make_box("Cabin", (CABIN_X, 0.0, cabin_z),
                     (CABIN_L, CABIN_W, CABIN_H), mat)
    taper_top(cabin, CABIN_TAPER)
    parts.append(cabin)

    # 前后保险杠
    for sx in (-1, 1):
        tag = "Front" if sx > 0 else "Rear"
        parts.append(make_box(
            f"Bumper_{tag}",
            (sx * BUMPER_X, 0.0, BODY_Z0 + BUMPER_H / 2.0 + 0.07),
            (BUMPER_L, BUMPER_W, BUMPER_H), mat))

    # 四个车轮（底面贴 z=0）
    for sx in (-1, 1):
        for sy in (-1, 1):
            tag_x = "F" if sx > 0 else "R"
            tag_y = "L" if sy > 0 else "R"
            parts.append(make_wheel(
                f"Wheel_{tag_x}{tag_y}",
                (sx * WHEEL_X, sy * WHEEL_Y, WHEEL_R), mat))

    # ── 合并为单一静态网格 ──
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    sm = bpy.context.object
    sm.name = "SM_Car"
    sm.data.name = "SM_Car_Mesh"

    # 应用变换（导出前必须烘焙旋转/缩放/位移进顶点坐标）
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    # UV 展开（流光材质多按 UV 扫描，必须有 UV Map）
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.02)
    bpy.ops.object.mode_set(mode='OBJECT')

    # 所有部件共用同一材质数据块，join 后应恰好收敛为 1 个槽位
    if len(sm.data.materials) != 1:
        raise RuntimeError(
            f"材质槽数应为 1，实际 {len(sm.data.materials)}："
            f"{[m.name if m else None for m in sm.data.materials]}")

    print(f"[合并结果] 顶点数: {len(sm.data.vertices)}  材质槽: {len(sm.data.materials)}")


# ── 注入 3D 视口上下文（解决 poll() context is incorrect）────────────────
# ⚠️ FBX 导出器不需要 VIEW_3D 上下文；在 temp_override 内部调用会导致部分
#    Blender 版本（4.1+）只写入材质数据而丢失网格几何体，故导出放在 override 外。
def run():
    for window in bpy.context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == 'VIEW_3D':
                for region in area.regions:
                    if region.type == 'WINDOW':
                        with bpy.context.temp_override(
                            window=window, area=area, region=region
                        ):
                            main()   # 建模 + UV，export 不在此处
                        break

    # ── FBX 导出（在 temp_override 范围外执行）──────────────────────────
    sm = bpy.data.objects.get("SM_Car")
    if sm is None:
        raise RuntimeError("SM_Car 对象未找到，main() 可能未正常执行"
                           "（请确认 Scripting 工作区包含 3D 视口面板）")

    bpy.ops.object.select_all(action='DESELECT')
    sm.select_set(True)
    bpy.context.view_layer.objects.active = sm

    # 规范：global_scale=1.0 + FBX_SCALE_UNITS，禁止 ×100
    export_path = "C:/EpicWkspc/LyraStarterGame/blender/fbx/SM_Car.fbx"
    bpy.ops.export_scene.fbx(
        filepath=export_path,
        use_selection=True,
        global_scale=1.0,
        apply_scale_options='FBX_SCALE_UNITS',    # 将 m→cm 烘焙进几何体
        axis_forward='-Z',
        axis_up='Y',
        object_types={'MESH'},
        bake_anim=False,
        path_mode='AUTO',
    )

    import os
    size_kb = os.path.getsize(export_path) // 1024
    print(f"SM_Car 导出完成: {export_path}")
    print(f"  文件大小: {size_kb} KB  （＜5KB 说明几何体仍为空）")
    print(f"  尺寸（UE5 cm）: L=430  W=185  H=150")
    print(f"  材质槽位: 1 → {sm.data.materials[0].name}（占位，UE5 内替换为流光材质）")


run()
