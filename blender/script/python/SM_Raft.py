"""Create the 200 x 200 x 150 cm OceanAdventure building/hull raft module."""

import os
from pathlib import Path

import bpy
from mathutils import Vector


MODULE_SIZE_M = 2.0
MODULE_HEIGHT_M = 1.5
HALF_EXTENT_M = (1.0, 1.0, 0.75)
ART_MARGIN_M = 0.04
DECK_LENGTH_M = 1.92
BOARD_COUNT = 7
BOARD_WIDTH_M = 0.27
BOARD_GAP_M = 0.005
BOARD_THICKNESS_M = 0.16
BOARD_CENTER_Z_M = 0.58
CROSSBEAM_LENGTH_M = 1.90
CROSSBEAM_WIDTH_M = 0.20
CROSSBEAM_HEIGHT_M = 0.18
CROSSBEAM_Y_M = (-0.65, 0.0, 0.65)
CROSSBEAM_CENTER_Z_M = 0.33
PONTOON_DIMENSIONS_M = (0.40, 0.50, 0.42)
PONTOON_X_M = (-0.73, 0.73)
PONTOON_Y_M = (-0.68, 0.68)
PONTOON_Z_M = -0.44

VISIBLE_COLLECTION = "SM_Raft_Generated"
COLLISION_COLLECTION = "SM_Raft_Collision_Generated"
STATIC_MESH_NAME = "SM_Raft"
COLLISION_NAME = "UCX_SM_Raft_00"


def project_root() -> Path:
    def valid(path):
        return path.is_dir() and any(path.glob("*.uproject")) and (
            path / "Plugins" / "GameFeatures" / "OceanAdventure"
        ).is_dir()

    override = os.environ.get("OCEAN_ADVENTURE_PROJECT_ROOT")
    if override:
        root = Path(override).expanduser().resolve()
        if valid(root):
            return root
        raise RuntimeError(f"Invalid OCEAN_ADVENTURE_PROJECT_ROOT: {root}")
    starts = [Path(__file__).resolve().parent, Path.cwd().resolve()]
    if bpy.data.filepath:
        starts.append(Path(bpy.data.filepath).resolve().parent)
    candidates = []
    seen = set()
    for start in starts:
        for path in (start, *start.parents):
            if path not in seen:
                seen.add(path)
                candidates.append(path)
    for path in candidates:
        if valid(path):
            return path
        try:
            for sibling in path.parent.iterdir():
                if sibling.is_dir() and valid(sibling):
                    return sibling
        except OSError:
            pass
    raise RuntimeError("Could not locate OceanAdventure project root")


def collection(name):
    value = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(value)
    return value


def remove_owned():
    for name in (VISIBLE_COLLECTION, COLLISION_COLLECTION):
        value = bpy.data.collections.get(name)
        if value is None:
            continue
        for obj in list(value.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.collections.remove(value)


def move(obj, target):
    for source in list(obj.users_collection):
        source.objects.unlink(obj)
    target.objects.link(obj)


def wood(name, color):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = next((node for node in mat.node_tree.nodes if node.type == "BSDF_PRINCIPLED"), None)
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (*color, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.78
    return mat


def box(name, dimensions, location, material, target, bevel=0.02):
    bpy.ops.mesh.primitive_cube_add(location=location)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = dimensions
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(material)
    modifier = obj.modifiers.new("EdgeBevel", "BEVEL")
    modifier.width = bevel
    modifier.segments = 3
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    move(obj, target)
    return obj


def bounds(obj):
    corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    return tuple(min(c[i] for c in corners) for i in range(3)), tuple(max(c[i] for c in corners) for i in range(3))


def build():
    remove_owned()
    visible = collection(VISIBLE_COLLECTION)
    collision_collection = collection(COLLISION_COLLECTION)
    materials = (
        wood("M_Raft_Wood_Light", (0.36, 0.16, 0.055)),
        wood("M_Raft_Wood_Mid", (0.27, 0.105, 0.032)),
        wood("M_Raft_Wood_Dark", (0.18, 0.060, 0.018)),
    )
    beam_material = wood("M_Raft_Crossbeam", (0.13, 0.040, 0.012))
    pontoon_material = wood("M_Raft_Pontoon", (0.20, 0.075, 0.020))
    parts = []
    total_width = BOARD_COUNT * BOARD_WIDTH_M + (BOARD_COUNT - 1) * BOARD_GAP_M
    first_x = -total_width * 0.5 + BOARD_WIDTH_M * 0.5
    for index in range(BOARD_COUNT):
        parts.append(box(
            f"Raft_Board_{index + 1:02d}",
            (BOARD_WIDTH_M, DECK_LENGTH_M, BOARD_THICKNESS_M),
            (first_x + index * (BOARD_WIDTH_M + BOARD_GAP_M), 0.0, BOARD_CENTER_Z_M),
            materials[index % len(materials)], visible, 0.025,
        ))
    for index, y in enumerate(CROSSBEAM_Y_M, 1):
        parts.append(box(
            f"Raft_Crossbeam_{index:02d}",
            (CROSSBEAM_LENGTH_M, CROSSBEAM_WIDTH_M, CROSSBEAM_HEIGHT_M),
            (0.0, y, CROSSBEAM_CENTER_Z_M), beam_material, visible, 0.025,
        ))
    for x in PONTOON_X_M:
        for y in PONTOON_Y_M:
            parts.append(box(
                f"Raft_Pontoon_{'L' if x < 0 else 'R'}_{'F' if y > 0 else 'B'}",
                PONTOON_DIMENSIONS_M, (x, y, PONTOON_Z_M), pontoon_material, visible, 0.08,
            ))

    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    raft = bpy.context.object
    raft.name = STATIC_MESH_NAME
    raft.data.name = f"{STATIC_MESH_NAME}_Mesh"
    bpy.context.scene.cursor.location = (0.0, 0.0, 0.0)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    raft["ue_asset_type"] = "StaticMesh"
    raft["module_dimensions_cm"] = "200 x 200 x 150"
    raft["module_origin"] = "collision_box_center"

    bpy.ops.mesh.primitive_cube_add(location=(0.0, 0.0, 0.0))
    collision = bpy.context.object
    collision.name = COLLISION_NAME
    collision.data.name = f"{COLLISION_NAME}_Mesh"
    collision.dimensions = (MODULE_SIZE_M, MODULE_SIZE_M, MODULE_HEIGHT_M)
    bpy.context.view_layer.objects.active = collision
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    collision.display_type = "WIRE"
    collision.show_name = True
    collision["ue_collision_for"] = STATIC_MESH_NAME
    collision["module_dimensions_cm"] = "200 x 200 x 150"
    move(collision, collision_collection)
    return raft, collision


def validate(raft, collision):
    minimum, maximum = bounds(raft)
    allowed_min = tuple(-extent + ART_MARGIN_M for extent in HALF_EXTENT_M)
    allowed_max = tuple(extent - ART_MARGIN_M for extent in HALF_EXTENT_M)
    for index, axis in enumerate("XYZ"):
        if minimum[index] < allowed_min[index] - 1e-4 or maximum[index] > allowed_max[index] + 1e-4:
            raise RuntimeError(f"{STATIC_MESH_NAME} exceeds 200x200x150 {axis} envelope")
    if any(abs(a - b) > 1e-4 for a, b in zip(collision.dimensions, (2.0, 2.0, 1.5))):
        raise RuntimeError(f"{COLLISION_NAME} must be exactly 2 x 2 x 1.5 m")
    print(f"Raft module bounds validated (m): min={minimum} max={maximum}")


def main():
    bpy.context.scene.unit_settings.system = "METRIC"
    bpy.context.scene.unit_settings.scale_length = 1.0
    bpy.context.scene.unit_settings.length_unit = "METERS"
    raft, collision = build()
    validate(raft, collision)
    output = project_root() / "blender" / "models"
    output.mkdir(parents=True, exist_ok=True)
    previous = bpy.context.preferences.filepaths.save_version
    bpy.context.preferences.filepaths.save_version = 0
    try:
        bpy.ops.wm.save_as_mainfile(filepath=str(output / "SM_Raft.blend"))
    finally:
        bpy.context.preferences.filepaths.save_version = previous
    bpy.ops.object.select_all(action="DESELECT")
    raft.select_set(True)
    collision.select_set(True)
    bpy.context.view_layer.objects.active = raft
    bpy.ops.export_scene.fbx(
        filepath=str(output / "SM_Raft.fbx"), use_selection=True, object_types={"MESH"},
        global_scale=1.0, apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
        axis_forward="-Z", axis_up="Y", use_mesh_modifiers=True, use_tspace=False,
        use_custom_props=True, add_leaf_bones=False, bake_anim=False, embed_textures=False,
    )
    print("[SM_Raft] Build complete: 200 x 200 x 150 cm")


if __name__ == "__main__":
    main()
