"""Create the compact emergency life raft used by DA_Raft_LifeRaft.

The mesh is intentionally smaller and visually distinct from SM_Raft: an orange
inflatable ring, a blue floor, reflective grab handles, and a small emergency flag.
It still uses the Raft building-module origin and collision envelope (200 x 200 x
150 cm) so the asset can be assigned to the same RaftActor without introducing a
second alignment convention.  DA_Raft_LifeRaft remains non-buildable because its
definition has no BuildPieceCatalog.

Run from Blender's Text Editor or with Blender's Python console.  The script is
idempotent: it only removes the collections it owns and writes:

    <project>/blender/models/SM_LifeRaft.blend
    <project>/blender/models/SM_LifeRaft.fbx
"""

import os
from pathlib import Path

import bpy
from mathutils import Vector


MODULE_SIZE_M = 2.0
MODULE_HEIGHT_M = 1.5
HALF_EXTENT_M = (1.0, 1.0, 0.75)
ART_MARGIN_M = 0.04

VISIBLE_COLLECTION = "SM_LifeRaft_Generated"
COLLISION_COLLECTION = "SM_LifeRaft_Collision_Generated"
STATIC_MESH_NAME = "SM_LifeRaft"
COLLISION_NAME = "UCX_SM_LifeRaft_00"
STANDARD_PROJECT_ROOT = Path("C:/EpicWkspc/OceanAdventure")


def project_root() -> Path:
    """Find OceanAdventure, never silently falling back to another UE project."""

    def valid(path: Path) -> bool:
        return path.is_dir() and any(path.glob("*.uproject")) and (
            path / "Plugins" / "GameFeatures" / "OceanAdventure"
        ).is_dir()

    override = os.environ.get("OCEAN_ADVENTURE_PROJECT_ROOT")
    if override:
        root = Path(override).expanduser().resolve()
        if valid(root):
            return root
        raise RuntimeError(f"Invalid OCEAN_ADVENTURE_PROJECT_ROOT: {root}")

    starts = [Path.cwd().resolve()]
    script_path = globals().get("__file__")
    if script_path:
        starts.append(Path(script_path).resolve().parent)
    if bpy.data.filepath:
        starts.append(Path(bpy.data.filepath).resolve().parent)

    candidates = []
    seen = set()
    for start in starts:
        for path in (start, *start.parents):
            if path in seen:
                continue
            seen.add(path)
            candidates.append(path)

    for path in candidates:
        if valid(path):
            return path
        # Several sibling Lyra copies may contain an OceanAdventure GameFeature.  When
        # Blender was launched from LyraStarterGame, prefer the explicitly named project
        # before considering structurally similar siblings such as LyraTemplate.
        preferred_sibling = path.parent / "OceanAdventure"
        if valid(preferred_sibling):
            return preferred_sibling
        try:
            for sibling in path.parent.iterdir():
                if sibling.is_dir() and valid(sibling):
                    return sibling
        except OSError:
            pass

    # Blender's Text Editor may expose an unsaved text block as ``\SM_LifeRaft.py``.
    # In that case __file__, cwd, and bpy.data.filepath contain no project anchor at all.
    # This repository's standard checkout is therefore an explicit, validated fallback;
    # the environment-variable override above still wins for relocated workspaces.
    standard_root = STANDARD_PROJECT_ROOT.resolve()
    if valid(standard_root):
        return standard_root

    raise RuntimeError(
        "Could not locate OceanAdventure project root. Set "
        "OCEAN_ADVENTURE_PROJECT_ROOT to C:/EpicWkspc/OceanAdventure."
    )


def collection(name: str):
    value = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(value)
    return value


def remove_owned() -> None:
    for name in (VISIBLE_COLLECTION, COLLISION_COLLECTION):
        value = bpy.data.collections.get(name)
        if value is None:
            continue
        for obj in list(value.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.collections.remove(value)


def move(obj, target) -> None:
    for source in list(obj.users_collection):
        source.objects.unlink(obj)
    target.objects.link(obj)


def material(name: str, color, roughness: float = 0.62, metallic: float = 0.0):
    value = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    value.use_nodes = True
    bsdf = next(
        (node for node in value.node_tree.nodes if node.type == "BSDF_PRINCIPLED"),
        None,
    )
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (*color, 1.0)
        bsdf.inputs["Roughness"].default_value = roughness
        if "Metallic" in bsdf.inputs:
            bsdf.inputs["Metallic"].default_value = metallic
    return value


def apply_bevel(obj, width: float, segments: int = 3) -> None:
    if width <= 0.0:
        return
    smallest = min(float(value) for value in obj.dimensions)
    safe_width = min(width, smallest * 0.42)
    if safe_width <= 1e-5:
        return
    modifier = obj.modifiers.new("EdgeBevel", "BEVEL")
    modifier.width = safe_width
    modifier.segments = segments
    modifier.limit_method = "ANGLE"
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=modifier.name)


def box(name: str, dimensions, location, mat, target, bevel: float = 0.02):
    bpy.ops.mesh.primitive_cube_add(location=location)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = dimensions
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(mat)
    apply_bevel(obj, bevel)
    move(obj, target)
    return obj


def cylinder(name: str, radius: float, depth: float, location, mat, target):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=24, radius=radius, depth=depth, location=location
    )
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.append(mat)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    apply_bevel(obj, min(radius * 0.28, 0.012), 2)
    move(obj, target)
    return obj


def torus(name: str, major_radius: float, minor_radius: float, location, scale, mat, target):
    bpy.ops.mesh.primitive_torus_add(
        major_segments=48,
        minor_segments=16,
        major_radius=major_radius,
        minor_radius=minor_radius,
        location=location,
    )
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(mat)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    move(obj, target)
    return obj


def flag_mesh(name: str, location, mat, target):
    """Create a tiny two-sided triangular-prism emergency pennant."""
    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    thickness = 0.008
    mesh.from_pydata(
        [
            (-thickness, 0.0, 0.0),
            (-thickness, 0.0, 0.16),
            (-thickness, 0.13, 0.105),
            (thickness, 0.0, 0.0),
            (thickness, 0.0, 0.16),
            (thickness, 0.13, 0.105),
        ],
        [],
        [
            (0, 2, 1),
            (3, 4, 5),
            (0, 1, 4, 3),
            (1, 2, 5, 4),
            (2, 0, 3, 5),
        ],
    )
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    target.objects.link(obj)
    obj.location = location
    obj.data.materials.append(mat)
    return obj


def build():
    remove_owned()
    visible = collection(VISIBLE_COLLECTION)
    collision_collection = collection(COLLISION_COLLECTION)

    orange = material("M_LifeRaft_Inflatable_Orange", (0.85, 0.095, 0.018), 0.55)
    orange_dark = material("M_LifeRaft_Inflatable_Shadow", (0.42, 0.025, 0.008), 0.62)
    blue = material("M_LifeRaft_Interior_Blue", (0.025, 0.13, 0.24), 0.70)
    reflective = material("M_LifeRaft_Reflective_White", (0.92, 0.95, 0.90), 0.35)
    emergency_yellow = material("M_LifeRaft_Emergency_Yellow", (1.0, 0.58, 0.015), 0.48)
    rope = material("M_LifeRaft_GrabRope", (0.06, 0.035, 0.02), 0.92)

    parts = []

    # Compact oval tube: about 150 x 110 cm, deliberately smaller than SM_Raft's
    # full 200 x 200 cm deck while remaining centred on the module origin.
    parts.append(
        torus(
            "LifeRaft_Inflatable_Ring",
            major_radius=0.49,
            minor_radius=0.135,
            location=(0.0, 0.0, 0.02),
            scale=(1.17, 0.86, 1.0),
            mat=orange,
            target=visible,
        )
    )

    # A darker lower tube gives the raft a readable waterline and separation from
    # the orange upper tube without adding a second actor or material dependency.
    parts.append(
        torus(
            "LifeRaft_Lower_RubRail",
            major_radius=0.505,
            minor_radius=0.035,
            location=(0.0, 0.0, -0.105),
            scale=(1.16, 0.85, 0.90),
            mat=orange_dark,
            target=visible,
        )
    )

    parts.append(box("LifeRaft_Interior_Floor", (0.82, 0.55, 0.055), (0.0, 0.0, -0.16), blue, visible, 0.035))
    parts.append(box("LifeRaft_Centre_Bench", (0.48, 0.075, 0.065), (0.0, 0.0, -0.03), orange_dark, visible, 0.018))

    # Four contrasting grab handles sit just outside the tube.  Their spacing makes
    # the silhouette read as rescue equipment rather than a generic wooden platform.
    for index, (x, y, rotation) in enumerate(
        ((0.0, 0.575, 0.0), (0.0, -0.575, 0.0), (0.665, 0.0, 1.5708), (-0.665, 0.0, 1.5708)),
        1,
    ):
        handle = box(
            f"LifeRaft_GrabHandle_{index:02d}",
            (0.19, 0.035, 0.035),
            (x, y, 0.10),
            reflective,
            visible,
            0.012,
        )
        handle.rotation_euler[2] = rotation
        parts.append(handle)

    # Two small reflective strips and four rope knots reinforce the emergency-rescue
    # identity while keeping the draw call as one imported static mesh after joining.
    for index, (x, y) in enumerate(((0.52, 0.0), (-0.52, 0.0)), 1):
        parts.append(box(f"LifeRaft_ReflectiveStrip_{index:02d}", (0.13, 0.045, 0.045), (x, y, 0.105), reflective, visible, 0.012))
    for index, (x, y) in enumerate(((0.57, 0.29), (0.57, -0.29), (-0.57, 0.29), (-0.57, -0.29)), 1):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=0.028, location=(x, y, -0.01))
        knot = bpy.context.object
        knot.name = f"LifeRaft_RopeKnot_{index:02d}"
        knot.data.materials.append(rope)
        move(knot, visible)
        parts.append(knot)

    # Folded canopy pack and a small yellow emergency pennant make the asset visibly
    # different from the construction raft at gameplay camera distance.
    parts.append(box("LifeRaft_CanopyPack", (0.22, 0.15, 0.12), (0.42, 0.0, 0.16), orange_dark, visible, 0.028))
    parts.append(cylinder("LifeRaft_FlagPole", 0.014, 0.38, (0.45, 0.0, 0.39), rope, visible))
    parts.append(flag_mesh("LifeRaft_EmergencyFlag", (0.45, 0.0, 0.53), emergency_yellow, visible))
    parts.append(cylinder("LifeRaft_InflationValve", 0.022, 0.07, (-0.48, 0.0, 0.18), rope, visible))

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
    raft["asset_role"] = "Emergency life raft; non-buildable"
    raft["module_dimensions_cm"] = "200 x 200 x 150"
    raft["visual_footprint_cm"] = "150 x 120 x 90"
    raft["module_origin"] = "collision_box_center"

    # Keep the standard module collision envelope.  ARaftActor uses the same authored
    # 200 x 200 x 150 cm deck box for both definitions; the life raft's smaller visual
    # is intentional and its BuildPieceCatalog is left empty by the UE asset script.
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


def bounds(obj):
    corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    return tuple(min(c[i] for c in corners) for i in range(3)), tuple(max(c[i] for c in corners) for i in range(3))


def validate(raft, collision):
    minimum, maximum = bounds(raft)
    allowed_min = tuple(-extent + ART_MARGIN_M for extent in HALF_EXTENT_M)
    allowed_max = tuple(extent - ART_MARGIN_M for extent in HALF_EXTENT_M)
    for index, axis in enumerate("XYZ"):
        if minimum[index] < allowed_min[index] - 1e-4 or maximum[index] > allowed_max[index] + 1e-4:
            raise RuntimeError(f"{STATIC_MESH_NAME} exceeds 200x200x150 {axis} envelope")
    if any(abs(a - b) > 1e-4 for a, b in zip(collision.dimensions, (2.0, 2.0, 1.5))):
        raise RuntimeError(f"{COLLISION_NAME} must be exactly 2 x 2 x 1.5 m")
    print(f"Life raft bounds validated (m): min={minimum} max={maximum}")


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
        bpy.ops.wm.save_as_mainfile(filepath=str(output / "SM_LifeRaft.blend"))
    finally:
        bpy.context.preferences.filepaths.save_version = previous

    bpy.ops.object.select_all(action="DESELECT")
    raft.select_set(True)
    collision.select_set(True)
    bpy.context.view_layer.objects.active = raft
    bpy.ops.export_scene.fbx(
        filepath=str(output / "SM_LifeRaft.fbx"),
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
    print("[SM_LifeRaft] Build complete: compact non-buildable emergency raft")


if __name__ == "__main__":
    main()
