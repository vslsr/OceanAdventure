"""Create and export a UE5-ready stylized heavy-weapon cannonball.

Run this file from Blender's Scripting workspace. It only replaces the generated
``Naval_Cannonball_Generated`` collection and exports one FBX:

    blender/models/
        SM_Naval_Cannonball.fbx

After export, run NavalCore's ``CreateNavalCoreCannon.py`` in Unreal Editor to import
the FBX into ``/NavalCore/Arts/Cannon/Meshes`` and configure the projectile Blueprint. The
shell belongs to the shared cannon in the framework plugin, not to a GameFeature,
because the field emplacement and the deck gun both fire it. The script is safe to
re-run and does not touch unrelated scene objects.
"""

import math
import os
from pathlib import Path

import bpy


COLLECTION_NAME = "Naval_Cannonball_Generated"
STATIC_MESH_NAME = "SM_Naval_Cannonball"
FBX_NAME = f"{STATIC_MESH_NAME}.fbx"


def find_project_root():
    """Find the OceanAdventure project from Blender, even when opened from LyraStarterGame.

    Blender often stores a text block under a pseudo path such as
    ``create_naval_cannonball.blend/create_naval_cannonball.py``. The old resolver stopped at
    the nearest ``LyraStarterGame.uproject`` and then failed because that project does not own
    the OceanAdventure GameFeature. We now identify the project by its GameFeature directory
    and also inspect sibling projects next to any discovered ``.uproject``.
    """

    def is_ocean_adventure_root(candidate):
        return (
            candidate.is_dir()
            and any(candidate.glob("*.uproject"))
            and (candidate / "Plugins" / "GameFeatures" / "OceanAdventure").is_dir()
        )

    override = os.environ.get("OCEAN_ADVENTURE_PROJECT_ROOT")
    if override:
        root = Path(override).expanduser().resolve()
        if is_ocean_adventure_root(root):
            return root
        raise RuntimeError(
            "OCEAN_ADVENTURE_PROJECT_ROOT does not point to an OceanAdventure project "
            "(expected a .uproject and Plugins/GameFeatures/OceanAdventure): "
            f"{root}"
        )

    starts = [Path.cwd().resolve()]
    if "__file__" in globals():
        starts.insert(0, Path(__file__).resolve().parent)
    if bpy.data.filepath:
        starts.insert(0, Path(bpy.data.filepath).resolve().parent)

    visited = set()
    search_roots = []
    for start in starts:
        for candidate in (start, *start.parents):
            if candidate in visited:
                continue
            visited.add(candidate)
            search_roots.append(candidate)

    # A blend opened from C:/EpicWkspc/LyraStarterGame is next to the real
    # C:/EpicWkspc/OceanAdventure project. Check those siblings without scanning the disk.
    sibling_candidates = []
    for candidate in search_roots:
        try:
            sibling_candidates.extend(
                child for child in candidate.parent.iterdir() if child.is_dir()
            )
        except OSError:
            continue

    for candidate in (*search_roots, *sibling_candidates):
        if is_ocean_adventure_root(candidate):
            return candidate

    raise RuntimeError(
        "Could not locate OceanAdventure (expected a .uproject plus "
        "Plugins/GameFeatures/OceanAdventure). Set OCEAN_ADVENTURE_PROJECT_ROOT to "
        r"C:\EpicWkspc\OceanAdventure before running this script."
    )


def set_ue_scene_units():
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"


def remove_generated_collection():
    collection = bpy.data.collections.get(COLLECTION_NAME)
    if collection is None:
        return
    for obj in list(collection.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.collections.remove(collection)


def create_collection():
    collection = bpy.data.collections.new(COLLECTION_NAME)
    bpy.context.scene.collection.children.link(collection)
    return collection


def move_to_collection(obj, collection):
    for source in list(obj.users_collection):
        source.objects.unlink(obj)
    collection.objects.link(obj)


def make_material(name, color, roughness, metallic):
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name=name)
    material.use_nodes = True
    shader = material.node_tree.nodes.get("Principled BSDF")
    if shader:
        shader.inputs["Base Color"].default_value = (*color, 1.0)
        shader.inputs["Roughness"].default_value = roughness
        shader.inputs["Metallic"].default_value = metallic
    return material


def finish_mesh(obj, collection, material, bevel_width=0.0, smooth=True):
    obj.data.materials.append(material)
    if bevel_width > 0.0:
        bevel = obj.modifiers.new("Cannonball_RoundedEdges", "BEVEL")
        bevel.width = bevel_width
        bevel.segments = 3
    if smooth:
        for polygon in obj.data.polygons:
            polygon.use_smooth = True
    move_to_collection(obj, collection)
    return obj


def add_cannonball_body(collection, iron):
    # 24 cm diameter: large enough to read as a heavy naval shell without relying on
    # component scale in Unreal. The projectile actor uses segment traces, not mesh collision.
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=32,
        ring_count=20,
        radius=0.12,
        location=(0.0, 0.0, 0.0),
    )
    body = bpy.context.object
    body.name = "Cannonball_IronBody"
    return finish_mesh(body, collection, iron)


def add_reinforcement_bands(collection, dark_iron):
    # Two shallow crossed bands keep the shell readable at gameplay distance while preserving
    # a round collision silhouette. They are visual only; the actor's sweep owns hit logic.
    for name, rotation in (
        ("Cannonball_Band_X", (0.0, math.radians(90.0), 0.0)),
        ("Cannonball_Band_Y", (math.radians(90.0), 0.0, 0.0)),
    ):
        bpy.ops.mesh.primitive_torus_add(
            major_radius=0.119,
            minor_radius=0.006,
            major_segments=32,
            minor_segments=8,
            location=(0.0, 0.0, 0.0),
            rotation=rotation,
        )
        band = bpy.context.object
        band.name = name
        finish_mesh(band, collection, dark_iron)


def add_fuse_plug(collection, brass, fuse):
    # Projectile forward is +X. The inset and short fuse sit at the rear so the actor can
    # orient the whole mesh to FlightVelocity without making the plug lead the shot.
    rotation = (0.0, math.radians(90.0), 0.0)
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=24,
        radius=0.030,
        depth=0.016,
        location=(-0.119, 0.0, 0.0),
        rotation=rotation,
    )
    plug = bpy.context.object
    plug.name = "Cannonball_FusePlug"
    finish_mesh(plug, collection, brass, bevel_width=0.003)

    bpy.ops.mesh.primitive_cylinder_add(
        vertices=16,
        radius=0.009,
        depth=0.028,
        location=(-0.139, 0.0, 0.0),
        rotation=rotation,
    )
    fuse_object = bpy.context.object
    fuse_object.name = "Cannonball_Fuse"
    finish_mesh(fuse_object, collection, fuse, bevel_width=0.002)


def join_static_mesh(collection):
    meshes = [obj for obj in collection.objects if obj.type == "MESH"]
    if not meshes:
        raise RuntimeError("Generated cannonball has no mesh objects")

    for obj in meshes:
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        for modifier in list(obj.modifiers):
            bpy.ops.object.modifier_apply(modifier=modifier.name)
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()

    static_mesh = bpy.context.object
    static_mesh.name = STATIC_MESH_NAME
    static_mesh.data.name = STATIC_MESH_NAME

    old_cursor = bpy.context.scene.cursor.location.copy()
    bpy.context.scene.cursor.location = (0.0, 0.0, 0.0)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    bpy.context.scene.cursor.location = old_cursor
    return static_mesh


def export_ue5_fbx(static_mesh):
    # Raw authoring/export files belong to the project-level Blender source directory. The
    # Unreal editor script later imports this FBX into the owning GameFeature's Content path.
    output_dir = find_project_root() / "blender" / "models"
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / FBX_NAME

    bpy.ops.object.select_all(action="DESELECT")
    static_mesh.select_set(True)
    bpy.context.view_layer.objects.active = static_mesh
    bpy.ops.export_scene.fbx(
        filepath=str(output_path),
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
        use_tspace=True,
        use_custom_props=True,
        add_leaf_bones=False,
        bake_anim=False,
        path_mode="AUTO",
        embed_textures=False,
    )

    if not output_path.is_file() or output_path.stat().st_size < 1024:
        raise RuntimeError(f"FBX export is missing or unexpectedly small: {output_path}")
    return output_path


def build_naval_cannonball():
    set_ue_scene_units()
    remove_generated_collection()
    collection = create_collection()

    iron = make_material("M_Cannonball_Iron", (0.055, 0.065, 0.070), 0.34, 0.86)
    dark_iron = make_material("M_Cannonball_DarkIron", (0.018, 0.024, 0.028), 0.42, 0.78)
    brass = make_material("M_Cannonball_FusePlug", (0.32, 0.16, 0.035), 0.36, 0.70)
    fuse = make_material("M_Cannonball_Fuse", (0.16, 0.070, 0.018), 0.88, 0.02)

    add_cannonball_body(collection, iron)
    add_reinforcement_bands(collection, dark_iron)
    add_fuse_plug(collection, brass, fuse)
    static_mesh = join_static_mesh(collection)
    output_path = export_ue5_fbx(static_mesh)

    print(f"Naval cannonball created in collection: {COLLECTION_NAME}")
    print(f"UE5 FBX exported: {output_path}")
    print(f"FBX size: {output_path.stat().st_size // 1024} KB")


if __name__ == "__main__":
    build_naval_cannonball()
