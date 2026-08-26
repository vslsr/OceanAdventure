"""Create the 160 x 160 x 140 cm attachable naval helm module.

The asset follows the cannon's WeaponCollision envelope.  The visible mesh is authored in
actor-root space with its root at the deck, while the runtime helm actor supplies the
authoritative 160 x 160 x 140 cm collision box.  Re-running the script only replaces the
collections and objects owned by this script.
"""

import math
import os
from pathlib import Path

import bpy
from mathutils import Vector


STATIC_MESH_NAME = "SM_Naval_HelmWheel"
COLLECTION_NAME = "Naval_HelmWheel_Generated"
GUIDE_COLLECTION_NAME = "Naval_HelmWheel_CollisionGuide"
FBX_NAME = f"{STATIC_MESH_NAME}.fbx"
BLEND_NAME = f"{STATIC_MESH_NAME}.blend"

COLLISION_HALF_EXTENT_M = (0.80, 0.80, 0.70)
COLLISION_CENTER_M = (0.0, 0.0, 0.70)
ART_MARGIN_M = 0.04


def find_project_root() -> Path:
    def is_project(candidate: Path) -> bool:
        return (
            candidate.is_dir()
            and any(candidate.glob("*.uproject"))
            and (candidate / "Plugins" / "GameFeatures" / "OceanAdventure").is_dir()
        )

    override = os.environ.get("OCEAN_ADVENTURE_PROJECT_ROOT")
    if override:
        root = Path(override).expanduser().resolve()
        if is_project(root):
            return root
        raise RuntimeError(f"Invalid OCEAN_ADVENTURE_PROJECT_ROOT: {root}")

    starts = []
    if "__file__" in globals():
        starts.append(Path(__file__).resolve().parent)
    starts.append(Path.cwd().resolve())
    if bpy.data.filepath:
        starts.append(Path(bpy.data.filepath).resolve().parent)

    visited = set()
    candidates = []
    for start in starts:
        for candidate in (start, *start.parents):
            if candidate not in visited:
                visited.add(candidate)
                candidates.append(candidate)
    siblings = []
    for candidate in candidates:
        try:
            siblings.extend(child for child in candidate.parent.iterdir() if child.is_dir())
        except OSError:
            pass
    for candidate in (*candidates, *siblings):
        if is_project(candidate):
            return candidate
    raise RuntimeError(
        "Could not locate OceanAdventure. Set OCEAN_ADVENTURE_PROJECT_ROOT to the project root."
    )


def remove_owned_collections() -> None:
    for name in (COLLECTION_NAME, GUIDE_COLLECTION_NAME):
        collection = bpy.data.collections.get(name)
        if collection is None:
            continue
        for obj in list(collection.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.collections.remove(collection)


def create_collection(name: str):
    collection = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(collection)
    return collection


def move_to_collection(obj, collection) -> None:
    for source in list(obj.users_collection):
        source.objects.unlink(obj)
    collection.objects.link(obj)


def material(name: str, color, roughness=0.7, metallic=0.0):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name=name)
    mat.use_nodes = True
    bsdf = next((node for node in mat.node_tree.nodes if node.type == "BSDF_PRINCIPLED"), None)
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (*color, 1.0)
        bsdf.inputs["Roughness"].default_value = roughness
        bsdf.inputs["Metallic"].default_value = metallic
    return mat


def box(name, dimensions, location, mat, collection, bevel=0.02):
    bpy.ops.mesh.primitive_cube_add(location=location)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = dimensions
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(mat)
    if bevel:
        modifier = obj.modifiers.new("RoundedEdges", "BEVEL")
        modifier.width = bevel
        modifier.segments = 3
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    move_to_collection(obj, collection)
    return obj


def cylinder_between(name, start, end, radius, mat, collection, vertices=16):
    start, end = Vector(start), Vector(end)
    direction = end - start
    midpoint = (start + end) * 0.5
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices,
        radius=radius,
        depth=direction.length,
        location=midpoint,
    )
    obj = bpy.context.object
    obj.name = name
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = direction.to_track_quat("Z", "Y")
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(mat)
    bevel = obj.modifiers.new("RoundedEdges", "BEVEL")
    bevel.width = min(radius * 0.25, 0.015)
    bevel.segments = 2
    bpy.ops.object.modifier_apply(modifier=bevel.name)
    move_to_collection(obj, collection)
    return obj


def create_wheel_parts(collection, wood, dark_wood, brass):
    parts = []
    # Pedestal and base sit on Z=0; the wheel is in the XZ plane and spins around Y.
    parts.append(box("Helm_BasePlate", (0.76, 0.50, 0.08), (0.0, 0.0, 0.08), dark_wood, collection, 0.025))
    parts.append(box("Helm_Pedestal", (0.34, 0.30, 0.40), (0.0, 0.0, 0.30), wood, collection, 0.025))
    wheel_center = Vector((0.0, 0.0, 0.88))
    bpy.ops.mesh.primitive_torus_add(
        major_radius=0.365,
        minor_radius=0.045,
        major_segments=32,
        minor_segments=10,
        location=wheel_center,
        rotation=(math.pi * 0.5, 0.0, 0.0),
    )
    rim = bpy.context.object
    rim.name = "Helm_Wheel_Rim"
    rim.data.materials.append(wood)
    move_to_collection(rim, collection)
    parts.append(rim)
    for index in range(8):
        angle = index * math.tau / 8.0
        endpoint = wheel_center + Vector((math.cos(angle) * 0.35, 0.0, math.sin(angle) * 0.35))
        parts.append(cylinder_between(f"Helm_Spoke_{index:02d}", wheel_center, endpoint, 0.022, wood, collection))
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=24,
        radius=0.10,
        depth=0.16,
        location=wheel_center,
        rotation=(math.pi * 0.5, 0.0, 0.0),
    )
    hub = bpy.context.object
    hub.name = "Helm_Wheel_Hub"
    hub.data.materials.append(brass)
    move_to_collection(hub, collection)
    parts.append(hub)
    return parts


def join_parts(parts):
    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    mesh = bpy.context.object
    mesh.name = STATIC_MESH_NAME
    mesh.data.name = f"{STATIC_MESH_NAME}_Mesh"
    cursor = bpy.context.scene.cursor.location.copy()
    bpy.context.scene.cursor.location = (0.0, 0.0, 0.0)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    bpy.context.scene.cursor.location = cursor
    mesh["ue_asset_type"] = "StaticMesh"
    mesh["module_dimensions_cm"] = "160 x 160 x 140"
    mesh["module_origin"] = "attachable_root"
    return mesh


def create_guide(collection):
    bpy.ops.mesh.primitive_cube_add(location=COLLISION_CENTER_M)
    guide = bpy.context.object
    guide.name = "BuildableModule_CollisionGuide_160x160x140cm"
    guide.dimensions = tuple(value * 2.0 for value in COLLISION_HALF_EXTENT_M)
    bpy.context.view_layer.objects.active = guide
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    guide.display_type = "WIRE"
    guide.hide_render = True
    move_to_collection(guide, collection)
    return guide


def bounds(obj):
    corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    return tuple(min(c[i] for c in corners) for i in range(3)), tuple(max(c[i] for c in corners) for i in range(3))


def validate_bounds(mesh):
    minimum, maximum = bounds(mesh)
    print(f"Helm wheel candidate bounds (m): min={minimum} max={maximum}")
    allowed_min = tuple(COLLISION_CENTER_M[i] - COLLISION_HALF_EXTENT_M[i] + ART_MARGIN_M for i in range(3))
    allowed_max = tuple(COLLISION_CENTER_M[i] + COLLISION_HALF_EXTENT_M[i] - ART_MARGIN_M for i in range(3))
    for i, axis in enumerate("XYZ"):
        if minimum[i] < allowed_min[i] - 1e-4 or maximum[i] > allowed_max[i] + 1e-4:
            raise RuntimeError(f"{STATIC_MESH_NAME} exceeds 160x160x140 {axis} envelope")
    print(f"Helm wheel bounds validated (m): min={minimum} max={maximum}")


def save_blend(path: Path):
    prefs = bpy.context.preferences.filepaths
    previous = prefs.save_version
    prefs.save_version = 0
    try:
        bpy.ops.wm.save_as_mainfile(filepath=str(path))
    finally:
        prefs.save_version = previous


def main():
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"
    remove_owned_collections()
    generated = create_collection(COLLECTION_NAME)
    guide_collection = create_collection(GUIDE_COLLECTION_NAME)
    wood = material("M_HelmWheel_Wood", (0.34, 0.095, 0.025), 0.80)
    dark_wood = material("M_HelmWheel_DarkWood", (0.12, 0.028, 0.008), 0.88)
    brass = material("M_HelmWheel_Brass", (0.46, 0.19, 0.035), 0.36, 0.74)
    mesh = join_parts(create_wheel_parts(generated, wood, dark_wood, brass))
    create_guide(guide_collection)
    validate_bounds(mesh)
    output_dir = find_project_root() / "blender" / "models"
    output_dir.mkdir(parents=True, exist_ok=True)
    save_blend(output_dir / BLEND_NAME)
    bpy.ops.object.select_all(action="DESELECT")
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = mesh
    bpy.ops.export_scene.fbx(
        filepath=str(output_dir / FBX_NAME),
        use_selection=True,
        object_types={"MESH"},
        global_scale=1.0,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_UNITS",
        axis_forward="-Z",
        axis_up="Y",
        use_mesh_modifiers=True,
        mesh_smooth_type="FACE",
        use_tspace=False,
        use_custom_props=True,
        add_leaf_bones=False,
        bake_anim=False,
        embed_textures=False,
    )
    print(f"Naval helm wheel created: {output_dir / FBX_NAME}")


if __name__ == "__main__":
    main()
