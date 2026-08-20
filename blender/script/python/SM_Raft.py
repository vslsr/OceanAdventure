"""Create a basic UE5-ready wooden raft and export it as FBX.

Run this file from Blender's Scripting workspace.  It creates:

* SM_Raft           - the visible static mesh
* UCX_SM_Raft_00    - a single stable simple collision box for Unreal Engine

The generated .blend and .fbx files are written to the Raft GameFeature's
ArtSource/Raft directory. This script intentionally does not create Unreal
assets; import the FBX into /Raft/Vehicles/Raft.
"""

from pathlib import Path
import math
import os
import random

import bpy


# -----------------------------------------------------------------------------
# Model settings (Blender metres; Unreal imports the result as centimetres)
# -----------------------------------------------------------------------------

RAFT_LENGTH = 12.0
BOARD_COUNT = 13
BOARD_WIDTH = 0.48
BOARD_GAP = 0.07
BOARD_THICKNESS = 0.30

CROSSBEAM_WIDTH = 0.32
CROSSBEAM_HEIGHT = 0.25
CROSSBEAM_Y_POSITIONS = (-4.5, -1.5, 1.5, 4.5)

BOARD_BEVEL = 0.035
CROSSBEAM_BEVEL = 0.025
RANDOM_SEED = 20260820

FBX_NAME = "SM_Raft.fbx"
BLEND_NAME = "SM_Raft.blend"

# Blender's Text Editor commonly reports __file__ as only "SM_Raft.py" when
# the script was pasted into a text block.  Keep the checked-out project path
# as a final fallback; OCEAN_ADVENTURE_PROJECT_ROOT can still override it when
# the workspace is moved.
DEFAULT_PROJECT_ROOT = Path(r"C:\EpicWkspc\OceanAdventure")


def find_project_root() -> Path:
    """Find the OceanAdventure root without depending on Blender's CWD."""

    override = os.environ.get("OCEAN_ADVENTURE_PROJECT_ROOT")
    if override:
        root = Path(override).expanduser().resolve()
        if (root / "LyraTemplate.uproject").is_file():
            return root
        raise RuntimeError(
            "OCEAN_ADVENTURE_PROJECT_ROOT does not contain LyraTemplate.uproject: "
            f"{root}"
        )

    starts = []
    if "__file__" in globals():
        starts.append(Path(__file__).resolve().parent)
    if bpy.data.filepath:
        starts.append(Path(bpy.data.filepath).resolve().parent)
    starts.append(Path.cwd().resolve())

    visited = set()
    for start in starts:
        for candidate in (start, *start.parents):
            if candidate in visited:
                continue
            visited.add(candidate)
            if (candidate / "LyraTemplate.uproject").is_file():
                return candidate

    if (DEFAULT_PROJECT_ROOT / "LyraTemplate.uproject").is_file():
        return DEFAULT_PROJECT_ROOT

    raise RuntimeError(
        "Could not locate the OceanAdventure project root. Set the "
        "OCEAN_ADVENTURE_PROJECT_ROOT environment variable and run again."
    )


def set_scene_units() -> None:
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"


def reset_scene() -> None:
    if bpy.context.object is not None and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)

    # Remove unused data left by a previous run so repeated execution is stable.
    for data_collection in (bpy.data.meshes, bpy.data.curves, bpy.data.materials):
        for data_block in list(data_collection):
            if data_block.users == 0:
                data_collection.remove(data_block)


def get_principled_bsdf(material):
    if not material.use_nodes or material.node_tree is None:
        return None
    for node in material.node_tree.nodes:
        if node.type == "BSDF_PRINCIPLED":
            return node
    return None


def make_wood_material(name: str, color: tuple[float, float, float]):
    material = bpy.data.materials.new(name=name)
    material.use_nodes = True

    bsdf = get_principled_bsdf(material)
    if bsdf is not None:
        base_color = bsdf.inputs.get("Base Color")
        roughness = bsdf.inputs.get("Roughness")
        metallic = bsdf.inputs.get("Metallic")

        if base_color is not None:
            base_color.default_value = (*color, 1.0)
        if roughness is not None:
            roughness.default_value = 0.78
        if metallic is not None:
            metallic.default_value = 0.0

    return material


def create_beveled_box(
    name: str,
    dimensions: tuple[float, float, float],
    location: tuple[float, float, float],
    rotation_z: float,
    material,
    bevel_width: float,
):
    bpy.ops.mesh.primitive_cube_add(location=location, rotation=(0.0, 0.0, rotation_z))
    obj = bpy.context.object
    obj.name = name
    obj.data.name = f"{name}_Mesh"
    obj.dimensions = dimensions

    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

    if material is not None:
        obj.data.materials.append(material)

    bevel = obj.modifiers.new(name="EdgeBevel", type="BEVEL")
    bevel.width = bevel_width
    bevel.segments = 3
    bevel.limit_method = "ANGLE"
    if hasattr(bevel, "harden_normals"):
        bevel.harden_normals = True
    bpy.ops.object.modifier_apply(modifier=bevel.name)

    return obj


def join_visible_parts(parts, total_width: float):
    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)

    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()

    raft = bpy.context.object
    raft.name = "SM_Raft"
    raft.data.name = "SM_Raft_Mesh"

    # Keep actor-space pontoon offsets intuitive: the raft origin is its centre.
    bpy.context.scene.cursor.location = (0.0, 0.0, 0.0)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR", center="MEDIAN")

    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(
        angle_limit=math.radians(66.0),
        island_margin=0.02,
        correct_aspect=True,
        scale_to_bounds=False,
    )
    bpy.ops.object.mode_set(mode="OBJECT")

    raft["ue_asset_type"] = "StaticMesh"
    raft["ue_forward_axis"] = "X"
    raft["dimensions_cm"] = (
        f"{RAFT_LENGTH * 100:.0f} x {total_width * 100:.0f} x "
        f"{(BOARD_THICKNESS + CROSSBEAM_HEIGHT) * 100:.0f}"
    )
    return raft


def create_collision(total_width: float):
    # Cover the board deck and its supporting crossbeams with one stable box.
    collision_height = BOARD_THICKNESS + CROSSBEAM_HEIGHT
    collision_z = (BOARD_THICKNESS * 0.5 - collision_height * 0.5)

    bpy.ops.mesh.primitive_cube_add(location=(0.0, 0.0, collision_z))
    collision = bpy.context.object
    collision.name = "UCX_SM_Raft_00"
    collision.data.name = "UCX_SM_Raft_00_Mesh"
    collision.dimensions = (
        total_width,
        RAFT_LENGTH - 0.04,
        collision_height,
    )

    bpy.context.view_layer.objects.active = collision
    collision.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    collision.display_type = "WIRE"
    collision.show_name = True
    collision["ue_collision_for"] = "SM_Raft"
    return collision


def build_raft():
    random.seed(RANDOM_SEED)

    wood_materials = (
        make_wood_material("M_Raft_Wood_Light", (0.36, 0.16, 0.055)),
        make_wood_material("M_Raft_Wood_Mid", (0.27, 0.105, 0.032)),
        make_wood_material("M_Raft_Wood_Dark", (0.18, 0.060, 0.018)),
    )
    beam_material = make_wood_material("M_Raft_Crossbeam", (0.13, 0.040, 0.012))

    total_width = (
        BOARD_COUNT * BOARD_WIDTH + (BOARD_COUNT - 1) * BOARD_GAP
    )
    first_x = -total_width * 0.5 + BOARD_WIDTH * 0.5

    visible_parts = []
    for index in range(BOARD_COUNT):
        x = first_x + index * (BOARD_WIDTH + BOARD_GAP)
        y_jitter = random.uniform(-0.018, 0.018)
        z_jitter = random.uniform(-0.010, 0.010)
        yaw = math.radians(random.uniform(-0.45, 0.45))

        board = create_beveled_box(
            name=f"Raft_Board_{index + 1:02d}",
            dimensions=(BOARD_WIDTH, RAFT_LENGTH, BOARD_THICKNESS),
            location=(x, y_jitter, z_jitter),
            rotation_z=yaw,
            material=wood_materials[index % len(wood_materials)],
            bevel_width=BOARD_BEVEL,
        )
        visible_parts.append(board)

    beam_z = -(BOARD_THICKNESS * 0.5 + CROSSBEAM_HEIGHT * 0.5)
    for index, y in enumerate(CROSSBEAM_Y_POSITIONS, start=1):
        beam = create_beveled_box(
            name=f"Raft_Crossbeam_{index:02d}",
            dimensions=(total_width + 0.12, CROSSBEAM_WIDTH, CROSSBEAM_HEIGHT),
            location=(0.0, y, beam_z),
            rotation_z=0.0,
            material=beam_material,
            bevel_width=CROSSBEAM_BEVEL,
        )
        visible_parts.append(beam)

    raft = join_visible_parts(visible_parts, total_width)
    collision = create_collision(total_width)
    return raft, collision


def save_and_export(raft, collision) -> tuple[Path, Path]:
    project_root = find_project_root()
    output_dir = (
        project_root
        / "Plugins"
        / "GameFeatures"
        / "Raft"
        / "ArtSource"
        / "Raft"
    )
    output_dir.mkdir(parents=True, exist_ok=True)

    blend_path = output_dir / BLEND_NAME
    fbx_path = output_dir / FBX_NAME

    bpy.ops.object.select_all(action="DESELECT")
    raft.select_set(True)
    collision.select_set(True)
    bpy.context.view_layer.objects.active = raft

    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))

    # Keep both the visible mesh and the UCX collision selected for export.
    bpy.ops.object.select_all(action="DESELECT")
    raft.select_set(True)
    collision.select_set(True)
    bpy.context.view_layer.objects.active = raft

    bpy.ops.export_scene.fbx(
        filepath=str(fbx_path),
        use_selection=True,
        object_types={"MESH"},
        global_scale=1.0,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_UNITS",
        use_space_transform=True,
        bake_space_transform=False,
        axis_forward="-Z",
        axis_up="Y",
        use_mesh_modifiers=True,
        mesh_smooth_type="FACE",
        use_custom_props=True,
        add_leaf_bones=False,
        bake_anim=False,
        path_mode="AUTO",
        embed_textures=False,
    )

    if not fbx_path.is_file() or fbx_path.stat().st_size < 1024:
        raise RuntimeError(f"FBX export is missing or unexpectedly small: {fbx_path}")

    return blend_path, fbx_path


def main() -> None:
    set_scene_units()
    reset_scene()
    raft, collision = build_raft()
    blend_path, fbx_path = save_and_export(raft, collision)

    print("[SM_Raft] Build complete")
    print(f"  Visible mesh : {raft.name}")
    print(f"  Collision    : {collision.name}")
    print(f"  Vertices     : {len(raft.data.vertices)}")
    print(f"  Polygons     : {len(raft.data.polygons)}")
    print(f"  Blend        : {blend_path}")
    print(f"  FBX          : {fbx_path}")
    print(f"  FBX size     : {fbx_path.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
