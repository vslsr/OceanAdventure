"""Create an animation-ready round character with detached floating hands.

Run this script from Blender's Scripting workspace. It only replaces the
``RoundBodyCharacter_Generated`` collection and exports one skeletal FBX:

    blender/models/SK_RoundBodyCharacter.fbx

The body, hands, feet, and face are combined into one skinned mesh. The left
and right hands remain separate geometry islands and are rigidly weighted to
independent, disconnected hand bones, so later animations can translate and
rotate them without needing arm geometry.
"""

import os
from pathlib import Path

import bpy


COLLECTION_NAME = "RoundBodyCharacter_Generated"
MESH_NAME = "SK_RoundBodyCharacter"
RIG_NAME = "SKEL_RoundBodyCharacter"
ARMATURE_DATA_NAME = "SKEL_RoundBodyCharacter_Armature"
FBX_NAME = f"{MESH_NAME}.fbx"
STANDARD_PROJECT_ROOT = Path(r"C:\EpicWkspc\OceanAdventure")


def is_ocean_adventure_root(candidate):
    """Return whether *candidate* is the OceanAdventure Unreal project root."""
    candidate = Path(candidate)
    return (
        candidate.is_dir()
        and any(candidate.glob("*.uproject"))
        and (candidate / "Plugins" / "GameFeatures" / "OceanAdventure").is_dir()
    )


def resolve_project_root(starts, override=None, fallback=STANDARD_PROJECT_ROOT):
    """Resolve OceanAdventure from explicit, ancestor, sibling, and fallback paths."""
    if override:
        root = Path(override).expanduser().resolve()
        if is_ocean_adventure_root(root):
            return root
        raise RuntimeError(
            "OCEAN_ADVENTURE_PROJECT_ROOT does not point to OceanAdventure "
            "(expected a .uproject and Plugins/GameFeatures/OceanAdventure): "
            f"{root}"
        )

    search_roots = []
    visited = set()
    for start in starts:
        if not start:
            continue
        start = Path(start).expanduser().resolve()
        for candidate in (start, *start.parents):
            if candidate in visited:
                continue
            visited.add(candidate)
            search_roots.append(candidate)

    sibling_candidates = []
    sibling_visited = set()
    for candidate in search_roots:
        try:
            siblings = list(candidate.parent.iterdir())
        except OSError:
            continue
        for sibling in siblings:
            if sibling in sibling_visited:
                continue
            sibling_visited.add(sibling)
            try:
                if sibling.is_dir():
                    sibling_candidates.append(sibling)
            except OSError:
                continue

    for candidate in search_roots:
        if is_ocean_adventure_root(candidate):
            return candidate

    # Multiple checkouts may contain the same GameFeature. When launched from a
    # sibling Lyra project, prefer the checkout explicitly named OceanAdventure.
    sibling_candidates.sort(
        key=lambda candidate: (
            candidate.name.casefold() != "oceanadventure",
            not (candidate / "OceanAdventure.uproject").is_file(),
            str(candidate).casefold(),
        )
    )
    for candidate in sibling_candidates:
        if is_ocean_adventure_root(candidate):
            return candidate

    fallback = Path(fallback).expanduser().resolve() if fallback else None
    if fallback and is_ocean_adventure_root(fallback):
        return fallback

    raise RuntimeError(
        "Could not locate OceanAdventure (expected a .uproject plus "
        "Plugins/GameFeatures/OceanAdventure). Set OCEAN_ADVENTURE_PROJECT_ROOT "
        r"to C:\EpicWkspc\OceanAdventure before running this script."
    )


def find_project_root():
    """Find the project even from an unsaved Blend or a Text Block pseudo path."""
    starts = [Path.cwd()]
    if "__file__" in globals():
        starts.insert(0, Path(__file__).parent)
    if bpy.data.filepath:
        starts.insert(0, Path(bpy.data.filepath).parent)
    return resolve_project_root(
        starts,
        override=os.environ.get("OCEAN_ADVENTURE_PROJECT_ROOT"),
    )


def set_ue_scene_units():
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"


def remove_generated_collection():
    """Delete only data previously owned by this script."""
    collection = bpy.data.collections.get(COLLECTION_NAME)
    if collection is None:
        return

    owned_data = []
    for obj in list(collection.objects):
        if obj.type in {"MESH", "ARMATURE"} and obj.data is not None:
            owned_data.append(obj.data)
        bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.collections.remove(collection)

    for data in owned_data:
        if data.users != 0:
            continue
        if isinstance(data, bpy.types.Mesh):
            bpy.data.meshes.remove(data)
        elif isinstance(data, bpy.types.Armature):
            bpy.data.armatures.remove(data)


def create_collection():
    collection = bpy.data.collections.new(COLLECTION_NAME)
    bpy.context.scene.collection.children.link(collection)
    return collection


def move_to_collection(obj, collection):
    for source in list(obj.users_collection):
        source.objects.unlink(obj)
    collection.objects.link(obj)


def make_material(name, color, roughness=0.55, metallic=0.0):
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name=name)
    material.use_nodes = True
    shader = material.node_tree.nodes.get("Principled BSDF")
    if shader:
        shader.inputs["Base Color"].default_value = (*color, 1.0)
        shader.inputs["Roughness"].default_value = roughness
        shader.inputs["Metallic"].default_value = metallic
    return material


def assign_rigid_weight(obj, bone_name):
    """Assign every vertex in one mesh part to exactly one deformation bone."""
    group = obj.vertex_groups.new(name=bone_name)
    group.add(range(len(obj.data.vertices)), 1.0, "REPLACE")


def finish_part(obj, collection, material, bone_name):
    obj.data.materials.append(material)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    assign_rigid_weight(obj, bone_name)
    move_to_collection(obj, collection)
    return obj


def add_sphere_part(
    collection,
    name,
    location,
    scale,
    material,
    bone_name,
    segments=32,
    ring_count=20,
):
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=segments,
        ring_count=ring_count,
        radius=1.0,
        location=location,
    )
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    return finish_part(obj, collection, material, bone_name)


def create_character_parts(collection):
    body_material = make_material("M_RoundCharacter_Body", (0.055, 0.42, 0.66), 0.48)
    hand_material = make_material("M_RoundCharacter_Hands", (0.95, 0.64, 0.28), 0.58)
    foot_material = make_material("M_RoundCharacter_Feet", (0.075, 0.09, 0.13), 0.62)
    eye_white = make_material("M_RoundCharacter_EyeWhite", (0.96, 0.98, 1.0), 0.38)
    face_dark = make_material("M_RoundCharacter_FaceDark", (0.012, 0.018, 0.028), 0.45)

    parts = []

    # The front silhouette is circular (equal X/Z radii), with a little less depth.
    parts.append(
        add_sphere_part(
            collection,
            "RoundCharacter_Body",
            (0.0, 0.0, 1.05),
            (0.82, 0.58, 0.82),
            body_material,
            "body",
            segments=48,
            ring_count=32,
        )
    )

    # Positive X is the character's left side. Hands have a clear air gap from the body.
    hand_specs = (
        ("L", "hand_l", 1.20, 1.02),
        ("R", "hand_r", -1.20, -1.02),
    )
    for side, bone_name, palm_x, thumb_x in hand_specs:
        parts.append(
            add_sphere_part(
                collection,
                f"RoundCharacter_Hand_{side}",
                (palm_x, -0.01, 1.13),
                (0.27, 0.17, 0.31),
                hand_material,
                bone_name,
                segments=32,
                ring_count=20,
            )
        )
        parts.append(
            add_sphere_part(
                collection,
                f"RoundCharacter_Thumb_{side}",
                (thumb_x, -0.09, 1.08),
                (0.12, 0.13, 0.16),
                hand_material,
                bone_name,
                segments=24,
                ring_count=16,
            )
        )

    for side, bone_name, x in (("L", "foot_l", 0.39), ("R", "foot_r", -0.39)):
        parts.append(
            add_sphere_part(
                collection,
                f"RoundCharacter_Foot_{side}",
                (x, -0.13, 0.18),
                (0.34, 0.43, 0.18),
                foot_material,
                bone_name,
                segments=32,
                ring_count=18,
            )
        )

    # The face looks toward Blender -Y, which exports as Unreal +X with the settings below.
    for side, x in (("L", 0.23), ("R", -0.23)):
        parts.append(
            add_sphere_part(
                collection,
                f"RoundCharacter_EyeWhite_{side}",
                (x, -0.545, 1.24),
                (0.115, 0.055, 0.16),
                eye_white,
                "body",
                segments=24,
                ring_count=16,
            )
        )
        parts.append(
            add_sphere_part(
                collection,
                f"RoundCharacter_Pupil_{side}",
                (x, -0.595, 1.225),
                (0.055, 0.03, 0.08),
                face_dark,
                "body",
                segments=20,
                ring_count=12,
            )
        )

    parts.append(
        add_sphere_part(
            collection,
            "RoundCharacter_Mouth",
            (0.0, -0.565, 0.87),
            (0.18, 0.035, 0.045),
            face_dark,
            "body",
            segments=24,
            ring_count=12,
        )
    )
    return parts


def create_armature(collection):
    bpy.ops.object.armature_add(enter_editmode=False, location=(0.0, 0.0, 0.0))
    rig = bpy.context.object
    rig.name = RIG_NAME
    rig.data.name = ARMATURE_DATA_NAME
    rig.show_in_front = True
    rig.data.display_type = "OCTAHEDRAL"
    move_to_collection(rig, collection)

    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    edit_bones = rig.data.edit_bones
    root = edit_bones[0]
    root.name = "root"
    root.head = (0.0, 0.0, 0.0)
    root.tail = (0.0, 0.0, 0.35)
    root.use_deform = False

    body = edit_bones.new("body")
    body.head = (0.0, 0.0, 0.35)
    body.tail = (0.0, 0.0, 1.70)
    body.parent = root
    body.use_connect = True

    for name, x in (("hand_l", 1.20), ("hand_r", -1.20)):
        hand = edit_bones.new(name)
        hand.head = (x, 0.0, 1.13)
        hand.tail = (x, 0.0, 1.43)
        hand.parent = body
        hand.use_connect = False

    for name, x in (("foot_l", 0.39), ("foot_r", -0.39)):
        foot = edit_bones.new(name)
        foot.head = (x, -0.04, 0.18)
        foot.tail = (x, -0.04, 0.45)
        foot.parent = root
        foot.use_connect = False

    bpy.ops.object.mode_set(mode="OBJECT")
    rig["character_type"] = "round_body_floating_hands"
    rig["animation_note"] = (
        "Animate hand_l and hand_r independently; both bones are parented to body "
        "but intentionally disconnected."
    )
    return rig


def join_character_mesh(parts, rig):
    if not parts:
        raise RuntimeError("Round character generation produced no mesh parts")

    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()

    mesh = bpy.context.object
    mesh.name = MESH_NAME
    mesh.data.name = f"{MESH_NAME}_Mesh"

    modifier = mesh.modifiers.new("RoundCharacter_Armature", "ARMATURE")
    modifier.object = rig
    modifier.use_vertex_groups = True

    mesh.parent = rig
    mesh.parent_type = "OBJECT"
    mesh.matrix_parent_inverse = rig.matrix_world.inverted()
    return mesh


def validate_character(mesh, rig):
    expected_bones = {"root", "body", "hand_l", "hand_r", "foot_l", "foot_r"}
    actual_bones = {bone.name for bone in rig.data.bones}
    missing_bones = expected_bones - actual_bones
    if missing_bones:
        raise RuntimeError(f"Generated rig is missing bones: {sorted(missing_bones)}")

    expected_groups = expected_bones - {"root"}
    actual_groups = {group.name for group in mesh.vertex_groups}
    missing_groups = expected_groups - actual_groups
    if missing_groups:
        raise RuntimeError(f"Generated mesh is missing vertex groups: {sorted(missing_groups)}")

    for hand_name in ("hand_l", "hand_r"):
        bone = rig.data.bones[hand_name]
        if bone.use_connect:
            raise RuntimeError(f"{hand_name} must remain disconnected for floating-hand animation")


def export_ue5_fbx(mesh, rig):
    output_dir = find_project_root() / "blender" / "models"
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / FBX_NAME

    bpy.ops.object.select_all(action="DESELECT")
    mesh.select_set(True)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.fbx(
        filepath=str(output_path),
        use_selection=True,
        object_types={"ARMATURE", "MESH"},
        global_scale=1.0,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_UNITS",
        use_space_transform=True,
        bake_space_transform=False,
        axis_forward="-Y",
        axis_up="Z",
        use_mesh_modifiers=True,
        mesh_smooth_type="FACE",
        use_tspace=True,
        add_leaf_bones=False,
        primary_bone_axis="Y",
        secondary_bone_axis="X",
        use_armature_deform_only=True,
        armature_nodetype="NULL",
        bake_anim=False,
        path_mode="AUTO",
        embed_textures=False,
    )

    if not output_path.is_file() or output_path.stat().st_size < 4096:
        raise RuntimeError(f"FBX export is missing or unexpectedly small: {output_path}")
    return output_path


def build_round_body_character():
    set_ue_scene_units()
    remove_generated_collection()
    collection = create_collection()

    parts = create_character_parts(collection)
    rig = create_armature(collection)
    mesh = join_character_mesh(parts, rig)
    validate_character(mesh, rig)
    output_path = export_ue5_fbx(mesh, rig)

    print(f"Round character created in collection: {COLLECTION_NAME}")
    print(f"Skeletal mesh: {MESH_NAME}")
    print("Bones: root, body, hand_l, hand_r, foot_l, foot_r")
    print("The hand bones are parented to body but intentionally disconnected.")
    print(f"UE5 skeletal FBX exported: {output_path}")
    print(f"FBX size: {output_path.stat().st_size // 1024} KB")


if __name__ == "__main__":
    build_round_body_character()
