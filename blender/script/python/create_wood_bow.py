"""Create an animation-ready wood bow whose draw pose is driven entirely by bones.

Run this script from Blender's Scripting workspace. It only replaces the
``WoodBow_Generated`` collection and exports one skeletal FBX:

    blender/models/SK_WoodBow.fbx

The riser, both limbs, and the string are combined into one skinned mesh. Nothing
about the draw is baked into geometry: pulling the bow rotates ``limb_upper`` /
``limb_lower`` and translates ``string_mid``, so the mesh is built once and the
runtime only writes bone transforms.

The string is the reason this rig exists. Its vertices are blended linearly
between ``string_mid`` and the tip bone on their side, which turns a straight
rest string into a clean V the moment ``string_mid`` moves back -- no per-frame
geometry rebuild, no spline component.

Coordinate convention (matches ``create_round_body_character.py``): +Z is up and
Blender -Y is the direction the character faces, which exports as Unreal +X. The
bow therefore stands along Z, the riser sits at Y=0 (nearest the target) and the
string hangs at +Y, on the archer's side. That is a real archer's grip: draw the
string toward +Y and the arrow leaves toward -Y.

The arrow's attach point is deliberately NOT a bone. ``string_mid`` is a deform
bone the draw animation writes every frame; content should attach to a Skeleton
socket named ``nock`` parented to it, created by the Unreal import script. A
socket survives later rig changes that a duplicate bone would not.
"""

import math
import os
from pathlib import Path

import bpy
import mathutils


COLLECTION_NAME = "WoodBow_Generated"
MESH_NAME = "SK_WoodBow"
RIG_NAME = "SKEL_WoodBow"
ARMATURE_DATA_NAME = "SKEL_WoodBow_Armature"
FBX_NAME = f"{MESH_NAME}.fbx"
STANDARD_PROJECT_ROOT = Path(r"C:\EpicWkspc\OceanAdventure")

# --- Design constants -------------------------------------------------------
# The two numbers a designer actually reasons about. Everything else about the
# limb arc is solved from them, so nudging the silhouette never means hand-
# editing a radius and an arc angle until they happen to agree.
#: Tip to tip along Z, metres. A shortbow a round character can hold.
BOW_TIP_TO_TIP = 1.05
#: Brace height: how far the resting string sits behind the riser, metres.
BOW_BRACE_HEIGHT = 0.22

#: Half the length of the rigid riser section, metres. Limbs start beyond it.
GRIP_HALF_LENGTH = 0.09
GRIP_RADIUS = 0.024
LIMB_ROOT_RADIUS = 0.018
LIMB_TIP_RADIUS = 0.009
STRING_RADIUS = 0.004

LIMB_SEGMENTS = 14
LIMB_SIDES = 8
STRING_SEGMENTS = 12
STRING_SIDES = 5

# --- Draw pose (mirrors SkyLand's RenderWeaponDraw.ts) ----------------------
# Only used by validate_draw_pose() to prove the rig deforms. The runtime curve
# lives in the Unreal AnimBlueprint; these are the full-draw endpoints it hits.
#: How far back each limb tip rotates at full draw, radians (16 degrees).
BOW_LIMB_BEND_RADIANS = math.radians(16.0)
#: How far the string's midpoint travels toward the archer at full draw, metres.
BOW_STRING_PULL = 0.18

#: The rig's bone contract, root first. Kept as a plain literal on purpose: the Unreal
#: import script reads this exact tuple out of this file with ast.literal_eval and refuses
#: an FBX whose skeleton disagrees. One list, two languages, no silent drift.
EXPECTED_BONES = (
    "root",
    "grip",
    "limb_upper",
    "limb_lower",
    "string_upper",
    "string_mid",
    "string_lower",
)
#: Everything below the motion root deforms geometry, so each needs a vertex group.
DEFORM_BONES = EXPECTED_BONES[1:]
EXPECTED_PARENTS = {
    "root": None,
    "grip": "root",
    "limb_upper": "grip",
    "limb_lower": "grip",
    # The tip bones ride their own limb, so the string follows the limbs for free.
    "string_upper": "limb_upper",
    "string_lower": "limb_lower",
    # The midpoint is the one thing the draw animation translates.
    "string_mid": "grip",
}


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


# --- Limb arc solver --------------------------------------------------------


def solve_limb_arc(tip_to_tip=BOW_TIP_TO_TIP, brace_height=BOW_BRACE_HEIGHT):
    """Solve the limb's circular arc from the two numbers a designer picks.

    The arc lies in the YZ plane, starts at the riser centre (0, 0, 0) and ends
    at one tip (0, brace_height, tip_to_tip / 2). Exactly one circle centred on
    the +Y axis passes through both, so the radius and the swept angle are
    derived rather than tuned:

        (b - R)^2 + s^2 = R^2   ->   R = (b^2 + s^2) / (2b)

    Returns ``(radius, half_arc_radians)``.
    """
    half_span = tip_to_tip * 0.5
    if brace_height <= 0.0 or half_span <= 0.0:
        raise RuntimeError(
            "Bow dimensions must be positive: "
            f"tip_to_tip={tip_to_tip}, brace_height={brace_height}"
        )
    radius = (brace_height * brace_height + half_span * half_span) / (2.0 * brace_height)
    half_arc = math.atan2(half_span, radius - brace_height)

    # A limb that curves past the string plane is a recurve, not this bow, and it
    # would let the string pass through the wood. Catch it here, not in Unreal.
    if not 0.0 < half_arc < math.pi * 0.5:
        raise RuntimeError(
            f"Solved limb arc is not a shortbow profile: half_arc={half_arc:.4f} rad. "
            f"Check BOW_TIP_TO_TIP={tip_to_tip} / BOW_BRACE_HEIGHT={brace_height}."
        )

    # The solver is the one place a silent silhouette error could hide, so read
    # the endpoint back instead of trusting the algebra.
    tip = arc_point(radius, half_arc)
    if abs(tip[1] - brace_height) > 1e-6 or abs(tip[2] - half_span) > 1e-6:
        raise RuntimeError(
            f"Limb arc solver disagrees with its own endpoint: {tip} != "
            f"(0.0, {brace_height}, {half_span})"
        )
    return radius, half_arc


def arc_point(radius, theta):
    """A point on the limb arc. theta=0 is the riser centre; +theta goes up."""
    return (0.0, radius * (1.0 - math.cos(theta)), radius * math.sin(theta))


def arc_theta_at_z(radius, z):
    """The arc parameter whose point sits at height *z*."""
    return math.asin(max(-1.0, min(1.0, z / radius)))


# --- Mesh construction ------------------------------------------------------


def build_tube(name, points, radii, sides):
    """Sweep a closed ring of *sides* along *points* and return the new object.

    The path always lies in the YZ plane, so the ring frame can use world X as a
    fixed reference axis. That removes the twist a general-purpose sweep has to
    solve for, and keeps the two limbs exactly mirror-symmetric.
    """
    if len(points) != len(radii):
        raise RuntimeError(f"{name}: {len(points)} path points but {len(radii)} radii")
    if len(points) < 2:
        raise RuntimeError(f"{name}: a tube needs at least two path points")

    vertices = []
    for index, (point, radius) in enumerate(zip(points, radii)):
        following = points[min(index + 1, len(points) - 1)]
        preceding = points[max(index - 1, 0)]
        tangent = _normalize(
            (
                following[0] - preceding[0],
                following[1] - preceding[1],
                following[2] - preceding[2],
            ),
            name,
        )
        axis_x = (1.0, 0.0, 0.0)
        axis_y = _normalize(_cross(tangent, axis_x), name)
        for side in range(sides):
            angle = 2.0 * math.pi * side / sides
            offset_x = math.cos(angle) * radius
            offset_y = math.sin(angle) * radius
            vertices.append(
                (
                    point[0] + axis_x[0] * offset_x + axis_y[0] * offset_y,
                    point[1] + axis_x[1] * offset_x + axis_y[1] * offset_y,
                    point[2] + axis_x[2] * offset_x + axis_y[2] * offset_y,
                )
            )

    faces = []
    for ring in range(len(points) - 1):
        base = ring * sides
        for side in range(sides):
            nxt = (side + 1) % sides
            faces.append((base + side, base + nxt, base + sides + nxt, base + sides + side))
    # Cap both ends; an open tube shows a hole at the bow tips and string ends.
    faces.append(tuple(range(sides - 1, -1, -1)))
    last = (len(points) - 1) * sides
    faces.append(tuple(range(last, last + sides)))

    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.validate()
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def _cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def _normalize(vector, name):
    length = math.sqrt(sum(component * component for component in vector))
    if length < 1e-9:
        raise RuntimeError(f"{name}: degenerate path direction {vector}")
    return tuple(component / length for component in vector)


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


def finish_part(obj, collection, material):
    obj.data.materials.append(material)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    # from_pydata winding is not guaranteed outward; fix it once, here.
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    move_to_collection(obj, collection)
    return obj


def create_riser(collection, material, radius, grip_theta):
    """The rigid handle section. It never bends, so it is one bone, rigidly weighted."""
    thetas = [
        -grip_theta + 2.0 * grip_theta * step / 6.0
        for step in range(7)
    ]
    points = [arc_point(radius, theta) for theta in thetas]
    obj = build_tube("WoodBow_Riser", points, [GRIP_RADIUS] * len(points), LIMB_SIDES)
    assign_rigid_weight(obj, "grip")
    return finish_part(obj, collection, material)


def create_limb(collection, material, radius, grip_theta, half_arc, upper):
    """One limb: rigidly weighted, because the whole limb swings as one piece.

    That is the same call SkyLand makes -- its limbs are two pivot Groups, not a
    bent spline -- and it keeps the draw pose to a single rotation per limb.
    """
    sign = 1.0 if upper else -1.0
    bone_name = "limb_upper" if upper else "limb_lower"
    points = []
    radii = []
    for step in range(LIMB_SEGMENTS + 1):
        blend = step / LIMB_SEGMENTS
        theta = grip_theta + (half_arc - grip_theta) * blend
        points.append(arc_point(radius, sign * theta))
        # Tapering toward the tip is not decoration: it is what reads as "limb"
        # rather than "bent stick" at the size this thing is held on screen.
        radii.append(LIMB_ROOT_RADIUS + (LIMB_TIP_RADIUS - LIMB_ROOT_RADIUS) * blend)
    obj = build_tube(f"WoodBow_{bone_name}", points, radii, LIMB_SIDES)
    assign_rigid_weight(obj, bone_name)
    return finish_part(obj, collection, material)


def create_string(collection, material, brace_height, half_span):
    """The string, blended between its midpoint bone and the tip bone above it.

    Weighting is linear in |z|. Under linear blend skinning a pure translation of
    ``string_mid`` therefore maps the straight rest string onto two straight
    segments -- an exact V, with no bulge in the middle and no extra bones. Get
    the blend wrong (constant weights, or a smoothstep) and the string bows out
    into an arc that no amount of animation tuning will straighten.
    """
    points = []
    for step in range(STRING_SEGMENTS + 1):
        blend = step / STRING_SEGMENTS
        points.append((0.0, brace_height, half_span - 2.0 * half_span * blend))
    obj = build_tube("WoodBow_String", points, [STRING_RADIUS] * len(points), STRING_SIDES)

    mid_group = obj.vertex_groups.new(name="string_mid")
    upper_group = obj.vertex_groups.new(name="string_upper")
    lower_group = obj.vertex_groups.new(name="string_lower")
    for index, vertex in enumerate(obj.data.vertices):
        toward_tip = min(1.0, abs(vertex.co.z) / half_span)
        mid_group.add([index], 1.0 - toward_tip, "REPLACE")
        tip_group = upper_group if vertex.co.z >= 0.0 else lower_group
        tip_group.add([index], toward_tip, "REPLACE")
    return finish_part(obj, collection, material)


def create_armature(collection, radius, grip_theta, half_arc, brace_height, half_span):
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
    root.tail = (0.0, 0.0, 0.12)
    # Unreal's motion root never deforms. use_armature_deform_only keeps it in the
    # FBX anyway, because it has deforming children.
    root.use_deform = False

    # The grip bone sits at the mesh origin so the hand socket and the asset
    # pivot are the same point; nothing downstream has to carry an offset.
    grip = edit_bones.new("grip")
    grip.head = (0.0, 0.0, 0.0)
    grip.tail = (0.0, 0.0, GRIP_HALF_LENGTH)
    grip.parent = root
    grip.use_connect = False

    limb_base_upper = arc_point(radius, grip_theta)
    limb_base_lower = arc_point(radius, -grip_theta)
    tip_upper = arc_point(radius, half_arc)
    tip_lower = arc_point(radius, -half_arc)

    for name, base, tip in (
        ("limb_upper", limb_base_upper, tip_upper),
        ("limb_lower", limb_base_lower, tip_lower),
    ):
        limb = edit_bones.new(name)
        # Head at the limb's own root, so the draw rotation happens where the
        # wood actually bends rather than at the middle of the grip.
        limb.head = base
        limb.tail = tip
        limb.parent = grip
        limb.use_connect = False

    # Tip bones ride their limb. The string end follows the limb swing for free,
    # which is why the draw animation only ever writes three transforms.
    for name, parent_name, tip_z in (
        ("string_upper", "limb_upper", half_span),
        ("string_lower", "limb_lower", -half_span),
    ):
        toward_mid = -0.06 if tip_z > 0.0 else 0.06
        end = edit_bones.new(name)
        end.head = (0.0, brace_height, tip_z)
        end.tail = (0.0, brace_height, tip_z + toward_mid)
        end.parent = edit_bones[parent_name]
        end.use_connect = False

    # The one bone the draw animation translates. Its tail points along +Y so the
    # draw axis is visible in the viewport instead of being folklore.
    string_mid = edit_bones.new("string_mid")
    string_mid.head = (0.0, brace_height, 0.0)
    string_mid.tail = (0.0, brace_height + 0.08, 0.0)
    string_mid.parent = grip
    string_mid.use_connect = False

    bpy.ops.object.mode_set(mode="OBJECT")
    rig["weapon_type"] = "wood_bow"
    rig["draw_note"] = (
        "Full draw = rotate limb_upper/limb_lower back by BOW_LIMB_BEND_RADIANS "
        "and translate string_mid by +Y BOW_STRING_PULL. Attach the arrow to a "
        "Skeleton socket named 'nock' on string_mid; do not add a nock bone."
    )
    return rig


def join_bow_mesh(parts, rig):
    if not parts:
        raise RuntimeError("Wood bow generation produced no mesh parts")

    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()

    mesh = bpy.context.object
    mesh.name = MESH_NAME
    mesh.data.name = f"{MESH_NAME}_Mesh"

    modifier = mesh.modifiers.new("WoodBow_Armature", "ARMATURE")
    modifier.object = rig
    modifier.use_vertex_groups = True

    mesh.parent = rig
    mesh.parent_type = "OBJECT"
    mesh.matrix_parent_inverse = rig.matrix_world.inverted()
    return mesh


# --- Validation -------------------------------------------------------------


def validate_bow(mesh, rig):
    """Static checks: the rig is the shape the Unreal side is written against."""
    actual_bones = {bone.name for bone in rig.data.bones}
    if actual_bones != set(EXPECTED_BONES):
        missing = sorted(set(EXPECTED_BONES) - actual_bones)
        extra = sorted(actual_bones - set(EXPECTED_BONES))
        raise RuntimeError(f"Generated rig bones are wrong: missing={missing} extra={extra}")

    for bone_name, parent_name in EXPECTED_PARENTS.items():
        bone = rig.data.bones[bone_name]
        actual_parent = bone.parent.name if bone.parent else None
        if actual_parent != parent_name:
            raise RuntimeError(
                f"{bone_name} must be parented to {parent_name}, found {actual_parent}"
            )

    if rig.data.bones["root"].use_deform:
        raise RuntimeError("root must stay non-deforming so it reads as Unreal's motion root")
    for bone_name in DEFORM_BONES:
        if not rig.data.bones[bone_name].use_deform:
            raise RuntimeError(f"{bone_name} must deform; it has skinned geometry")

    # A connected bone cannot be translated away from its parent's tail, which is
    # exactly what the draw does to string_mid.
    for bone_name in ("string_mid", "string_upper", "string_lower", "limb_upper", "limb_lower"):
        if rig.data.bones[bone_name].use_connect:
            raise RuntimeError(f"{bone_name} must stay disconnected so the draw can move it")

    actual_groups = {group.name for group in mesh.vertex_groups}
    if actual_groups != set(DEFORM_BONES):
        missing = sorted(set(DEFORM_BONES) - actual_groups)
        extra = sorted(actual_groups - set(DEFORM_BONES))
        raise RuntimeError(f"Generated mesh vertex groups are wrong: missing={missing} extra={extra}")

    group_names = {group.index: group.name for group in mesh.vertex_groups}
    for vertex in mesh.data.vertices:
        total = sum(element.weight for element in vertex.groups)
        if abs(total - 1.0) > 1e-4:
            owners = sorted(group_names[element.group] for element in vertex.groups)
            raise RuntimeError(
                f"Vertex {vertex.index} at {tuple(round(c, 4) for c in vertex.co)} has "
                f"total weight {total:.5f} across {owners}; skinning must be normalized"
            )

    if len(mesh.data.materials) != 2:
        raise RuntimeError(
            f"Expected wood + string materials, found {len(mesh.data.materials)} slots"
        )


def to_bone_space(pose_bone, world_direction):
    """Express a world-space direction in a pose bone's own basis space.

    PoseBone.location and .rotation_quaternion are defined in bone space, where local Y
    runs head->tail and local X/Z depend on the bone's roll. Writing world components
    straight into them silently pushes along the wrong axis -- ledger PY-BLENDER-001:
    pulling the string "along +Y" as a local Z offset moved it exactly perpendicular to
    the draw, so the validator measured a 0.0000m pull on a rig that was fine.
    """
    rest_orientation = pose_bone.bone.matrix_local.to_3x3()
    return rest_orientation.inverted() @ mathutils.Vector(world_direction)


def validate_draw_pose(mesh, rig, brace_height, half_span):
    """Pose the rig at full draw and read the deformed mesh back.

    This is the check worth having. Every assertion above can pass on a rig whose
    weights are silently wrong -- bones exist, groups exist, weights sum to one --
    and the bow will still stand dead still when the animation runs. So drive the
    rig the way the game will and confirm the vertices actually moved.
    """
    rest_string_mid = [
        vertex.index
        for vertex in mesh.data.vertices
        if abs(vertex.co.y - brace_height) < STRING_RADIUS * 2.0 and abs(vertex.co.z) < 0.02
    ]
    rest_grip = [
        vertex.index
        for vertex in mesh.data.vertices
        if abs(vertex.co.z) < GRIP_HALF_LENGTH * 0.5 and vertex.co.y < brace_height * 0.5
    ]
    if not rest_string_mid:
        raise RuntimeError("Could not locate string midpoint vertices to validate the draw")
    if not rest_grip:
        raise RuntimeError("Could not locate riser vertices to validate the draw")

    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="POSE")
    try:
        for bone_name, sign in (("limb_upper", 1.0), ("limb_lower", -1.0)):
            pose_bone = rig.pose.bones[bone_name]
            pose_bone.rotation_mode = "QUATERNION"
            # Swing the tip toward the archer about world X. The lower limb mirrors
            # it, the same sign flip SkyLand applies to its two pivot groups.
            axis = to_bone_space(pose_bone, (1.0, 0.0, 0.0))
            pose_bone.rotation_quaternion = mathutils.Quaternion(
                axis, sign * BOW_LIMB_BEND_RADIANS
            )
        # Draw the string toward the archer, +Y in world space.
        rig.pose.bones["string_mid"].location = to_bone_space(
            rig.pose.bones["string_mid"], (0.0, BOW_STRING_PULL, 0.0)
        )
        bpy.context.view_layer.update()

        depsgraph = bpy.context.evaluated_depsgraph_get()
        posed = mesh.evaluated_get(depsgraph).to_mesh()
        try:
            drawn_y = sum(posed.vertices[i].co.y for i in rest_string_mid) / len(rest_string_mid)
            grip_y = sum(posed.vertices[i].co.y for i in rest_grip) / len(rest_grip)
            rest_grip_y = sum(mesh.data.vertices[i].co.y for i in rest_grip) / len(rest_grip)
            # Total displacement as well as the Y component: without it, "the string
            # never moved" and "the string moved the wrong way" read identically.
            travelled = max(
                (posed.vertices[i].co - mesh.data.vertices[i].co).length
                for i in rest_string_mid
            )
        finally:
            mesh.evaluated_get(depsgraph).to_mesh_clear()

        pulled = drawn_y - brace_height
        if abs(pulled - BOW_STRING_PULL) > BOW_STRING_PULL * 0.1:
            raise RuntimeError(
                f"String midpoint moved {pulled:.4f}m along +Y at full draw, expected "
                f"{BOW_STRING_PULL:.4f}m (total displacement {travelled:.4f}m). "
                + (
                    "It moved, but not toward the archer: check the bone-space conversion "
                    "in to_bone_space()."
                    if travelled > BOW_STRING_PULL * 0.1
                    else "It did not move at all: the string_mid weights are not reaching it."
                )
            )
        if abs(grip_y - rest_grip_y) > 1e-3:
            raise RuntimeError(
                f"Riser moved {abs(grip_y - rest_grip_y):.4f}m during the draw; the grip "
                "must stay put or the bow will swim in the archer's hand."
            )
    finally:
        bpy.ops.pose.select_all(action="SELECT")
        bpy.ops.pose.transforms_clear()
        bpy.ops.object.mode_set(mode="OBJECT")
        bpy.context.view_layer.update()


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


def build_wood_bow():
    set_ue_scene_units()
    remove_generated_collection()
    collection = create_collection()

    radius, half_arc = solve_limb_arc()
    grip_theta = arc_theta_at_z(radius, GRIP_HALF_LENGTH)
    if grip_theta >= half_arc:
        raise RuntimeError(
            f"GRIP_HALF_LENGTH={GRIP_HALF_LENGTH} leaves no limb on a "
            f"{BOW_TIP_TO_TIP}m bow; shorten the riser or lengthen the bow."
        )
    half_span = BOW_TIP_TO_TIP * 0.5

    wood = make_material("M_WoodBow_Wood", (0.28, 0.16, 0.07), 0.62)
    cord = make_material("M_WoodBow_String", (0.86, 0.82, 0.70), 0.78)

    parts = [
        create_riser(collection, wood, radius, grip_theta),
        create_limb(collection, wood, radius, grip_theta, half_arc, upper=True),
        create_limb(collection, wood, radius, grip_theta, half_arc, upper=False),
        create_string(collection, cord, BOW_BRACE_HEIGHT, half_span),
    ]
    rig = create_armature(collection, radius, grip_theta, half_arc, BOW_BRACE_HEIGHT, half_span)
    mesh = join_bow_mesh(parts, rig)
    validate_bow(mesh, rig)
    validate_draw_pose(mesh, rig, BOW_BRACE_HEIGHT, half_span)
    output_path = export_ue5_fbx(mesh, rig)

    print(f"Wood bow created in collection: {COLLECTION_NAME}")
    print(f"Skeletal mesh: {MESH_NAME}  ({len(mesh.data.vertices)} verts)")
    print(f"Bones: {', '.join(EXPECTED_BONES)}")
    print(f"Limb arc solved: radius={radius:.4f}m half_arc={math.degrees(half_arc):.2f}deg")
    print(
        f"Draw pose verified: string_mid +Y {BOW_STRING_PULL}m, limbs "
        f"{math.degrees(BOW_LIMB_BEND_RADIANS):.1f}deg"
    )
    print("Attach the arrow to an Unreal Skeleton socket named 'nock' on string_mid.")
    print(f"UE5 skeletal FBX exported: {output_path}")
    print(f"FBX size: {output_path.stat().st_size // 1024} KB")


if __name__ == "__main__":
    build_wood_bow()
