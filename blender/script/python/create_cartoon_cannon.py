"""Create and export a UE5-ready stylized cannon as an FBX static mesh.

Run from Blender's Scripting workspace. The output is written into the owning
OceanAdventure GameFeature under ArtSource/Naval/GroundCannon. Re-running
replaces only the generated collection and output file, leaving unrelated
scene objects untouched.
"""

import math
import os
from pathlib import Path

import bpy


COLLECTION_NAME = "Cartoon_Cannon_Generated"
FBX_NAME = "SM_Naval_GroundCannon.fbx"


def find_project_root():
    """Locate the project even when Blender runs the script from a text block."""
    override = os.environ.get("OCEAN_ADVENTURE_PROJECT_ROOT")
    if override:
        root = Path(override).expanduser().resolve()
        if (root / "LyraTemplate.uproject").is_file():
            return root
        raise RuntimeError(
            "OCEAN_ADVENTURE_PROJECT_ROOT does not contain LyraTemplate.uproject: "
            f"{root}"
        )

    starts = [Path.cwd().resolve()]
    if "__file__" in globals():
        starts.insert(0, Path(__file__).resolve().parent)
    if bpy.data.filepath:
        starts.insert(0, Path(bpy.data.filepath).resolve().parent)

    visited = set()
    for start in starts:
        for candidate in (start, *start.parents):
            if candidate in visited:
                continue
            visited.add(candidate)
            if (candidate / "LyraTemplate.uproject").is_file():
                return candidate

    raise RuntimeError(
        "Could not locate OceanAdventure. Set OCEAN_ADVENTURE_PROJECT_ROOT "
        "to the project directory and run the script again."
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


def make_material(name, color, roughness=0.65, metallic=0.0):
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name=name)
    material.use_nodes = True
    bsdf = material.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        base = bsdf.inputs.get("Base Color")
        if base:
            base.default_value = (*color, 1.0)
        rough = bsdf.inputs.get("Roughness")
        if rough:
            rough.default_value = roughness
        metal = bsdf.inputs.get("Metallic")
        if metal:
            metal.default_value = metallic
    return material


def finish_mesh(obj, collection, material, bevel=0.06, smooth=True):
    obj.data.materials.append(material)
    if bevel > 0.0:
        modifier = obj.modifiers.new("Cartoon_Rounded_Edges", "BEVEL")
        modifier.width = bevel
        modifier.segments = 3
    if smooth:
        for polygon in obj.data.polygons:
            polygon.use_smooth = True
    move_to_collection(obj, collection)
    return obj


def add_cube(collection, name, location, scale, material, bevel=0.08, rotation=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(location=location, rotation=rotation)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    return finish_mesh(obj, collection, material, bevel=bevel, smooth=False)


def add_cylinder(
    collection,
    name,
    radius,
    depth,
    location,
    rotation,
    material,
    vertices=24,
    bevel=0.05,
):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices,
        radius=radius,
        depth=depth,
        location=location,
        rotation=rotation,
    )
    obj = bpy.context.object
    obj.name = name
    return finish_mesh(obj, collection, material, bevel=bevel)


def add_wheels(collection, wood, rim, hub):
    for side, y in (("Left", -0.94), ("Right", 0.94)):
        rotation = (math.radians(90), 0, 0)
        add_cylinder(
            collection,
            f"Cannon_Wheel_{side}",
            radius=0.78,
            depth=0.25,
            location=(-0.28, y, 0.79),
            rotation=rotation,
            material=wood,
            vertices=16,
            bevel=0.07,
        )
        add_cylinder(
            collection,
            f"Cannon_Rim_{side}",
            radius=0.81,
            depth=0.10,
            location=(-0.28, y * 1.075, 0.79),
            rotation=rotation,
            material=rim,
            vertices=16,
            bevel=0.035,
        )
        add_cylinder(
            collection,
            f"Cannon_Hub_{side}",
            radius=0.23,
            depth=0.37,
            location=(-0.28, y * 1.02, 0.79),
            rotation=rotation,
            material=hub,
            vertices=16,
            bevel=0.045,
        )


def add_carriage(collection, wood, dark_wood, metal):
    add_cube(collection, "Cannon_Carriage", (-0.22, 0, 0.72), (0.92, 0.68, 0.27), wood, 0.13)
    add_cube(collection, "Cannon_Carriage_Nose", (0.66, 0, 0.82), (0.38, 0.50, 0.19), dark_wood, 0.10)
    add_cube(
        collection,
        "Cannon_Trail",
        (-1.15, 0, 0.44),
        (0.88, 0.22, 0.17),
        dark_wood,
        0.09,
        rotation=(0, math.radians(-9), 0),
    )
    add_cylinder(
        collection,
        "Cannon_Axle",
        radius=0.15,
        depth=2.18,
        location=(-0.28, 0, 0.79),
        rotation=(math.radians(90), 0, 0),
        material=metal,
        vertices=16,
        bevel=0.035,
    )


def add_barrel(collection, bronze, dark_metal, black):
    # The barrel points along +X and tilts upward. Its profile is intentionally
    # short and chunky, with a strongly exaggerated bell around the muzzle.
    tilt = math.radians(11)
    rotation = (0, math.radians(90) - tilt, 0)

    add_cylinder(
        collection,
        "Cannon_Barrel_Main",
        radius=0.43,
        depth=1.55,
        location=(0.30, 0, 1.43),
        rotation=rotation,
        material=bronze,
        vertices=32,
        bevel=0.10,
    )
    add_cylinder(
        collection,
        "Cannon_Barrel_Rear",
        radius=0.51,
        depth=0.36,
        location=(-0.48, 0, 1.28),
        rotation=rotation,
        material=bronze,
        vertices=32,
        bevel=0.09,
    )
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, location=(-0.72, 0, 1.23))
    knob = bpy.context.object
    knob.name = "Cannon_Rear_Knob"
    knob.scale = (0.24, 0.24, 0.24)
    finish_mesh(knob, collection, bronze, bevel=0.0)

    # Oversized muzzle collar and a dark inset disk create a deep, readable bore.
    add_cylinder(
        collection,
        "Cannon_Muzzle_Bell",
        radius=0.66,
        depth=0.39,
        location=(1.16, 0, 1.60),
        rotation=rotation,
        material=bronze,
        vertices=32,
        bevel=0.11,
    )
    add_cylinder(
        collection,
        "Cannon_Muzzle_Lip",
        radius=0.72,
        depth=0.16,
        location=(1.41, 0, 1.65),
        rotation=rotation,
        material=dark_metal,
        vertices=32,
        bevel=0.07,
    )
    add_cylinder(
        collection,
        "Cannon_Bore",
        radius=0.50,
        depth=0.035,
        location=(1.505, 0, 1.67),
        rotation=rotation,
        material=black,
        vertices=32,
        bevel=0.015,
    )

    # Side trunnions visually lock the barrel into its wooden carriage.
    for side, y in (("Left", -0.54), ("Right", 0.54)):
        add_cylinder(
            collection,
            f"Cannon_Trunnion_{side}",
            radius=0.18,
            depth=0.25,
            location=(-0.05, y, 1.36),
            rotation=(math.radians(90), 0, 0),
            material=dark_metal,
            vertices=20,
            bevel=0.04,
        )


def join_static_mesh(collection):
    """Apply procedural modifiers and combine all parts into one UE static mesh."""
    meshes = [obj for obj in collection.objects if obj.type == "MESH"]
    if not meshes:
        raise RuntimeError("Cartoon cannon contains no mesh objects to export")

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
    static_mesh.name = "SM_CartoonCannon"
    static_mesh.data.name = "SM_CartoonCannon"

    # UE uses the exported object origin as the asset pivot. Keep it centered on
    # the ground between the wheels for predictable placement and rotation.
    old_cursor = bpy.context.scene.cursor.location.copy()
    bpy.context.scene.cursor.location = (0.0, 0.0, 0.0)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    bpy.context.scene.cursor.location = old_cursor
    return static_mesh


def export_ue5_fbx(static_mesh):
    output_dir = (
        find_project_root()
        / "Plugins"
        / "GameFeatures"
        / "OceanAdventure"
        / "ArtSource"
        / "Naval"
        / "GroundCannon"
    )
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


def build_cartoon_cannon():
    set_ue_scene_units()
    remove_generated_collection()
    collection = create_collection()

    bronze = make_material("M_Cannon_Bronze", (0.20, 0.43, 0.34), roughness=0.34, metallic=0.72)
    dark_metal = make_material("M_Cannon_DarkMetal", (0.035, 0.055, 0.050), roughness=0.40, metallic=0.82)
    bore = make_material("M_Cannon_Bore", (0.004, 0.006, 0.005), roughness=0.98)
    wood = make_material("M_Cannon_Wood", (0.43, 0.16, 0.045), roughness=0.78)
    dark_wood = make_material("M_Cannon_DarkWood", (0.20, 0.055, 0.016), roughness=0.86)
    wheel_rim = make_material("M_Cannon_WheelRim", (0.055, 0.067, 0.062), roughness=0.46, metallic=0.68)

    add_carriage(collection, wood, dark_wood, dark_metal)
    add_wheels(collection, wood, wheel_rim, bronze)
    add_barrel(collection, bronze, dark_metal, bore)

    static_mesh = join_static_mesh(collection)
    output_path = export_ue5_fbx(static_mesh)

    print("Cartoon cannon created in collection: Cartoon_Cannon_Generated")
    print(f"UE5 FBX exported: {output_path}")
    print(f"FBX size: {output_path.stat().st_size // 1024} KB")


if __name__ == "__main__":
    build_cartoon_cannon()
