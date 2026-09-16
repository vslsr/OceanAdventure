"""Create an animation-ready wood bow whose draw pose is driven entirely by bones.

Run this script from Blender's Scripting workspace. It only replaces the
``WoodBow_Generated`` collection and exports one skeletal FBX:

    blender/models/SK_WoodBow.fbx          mesh + skeleton, no animation
    blender/models/A_WoodBow_Clips.fbx     all four clips, one animation stack each

The riser, both limbs, and the string are combined into one skinned mesh. Nothing
about the draw is baked into geometry: pulling the bow rotates ``limb_upper`` /
``limb_lower`` and translates ``string_mid``, so the mesh is built once and the
runtime only writes bone transforms.

Four clips come out with it, covering the states the bow is actually in -- idle, drawing,
aiming, shooting -- and they are driven three different ways on purpose.

The draw is a *pose*, not a performance: two linear keyframes from braced to full draw,
which Unreal samples at an explicit time of ``charge`` rather than playing back. Any
easing belongs to the charge curve, so baking easing into these keys would apply it twice.

The release is the opposite -- a damped wobble that only makes sense against the clock --
so it is baked per frame from the same curves SkyLand evaluates per render frame.

Idle and aim are the third kind: loops the bow sits in for as long as the player leaves it
there. Idle breathes the braced string forward and back; aim trembles around full draw,
because a held bow that is perfectly still reads as a paused game. Both are sampled over a
whole number of cycles and close exactly on their first key, and both are authored so that
their first frame *is* the neighbouring clip's end pose: idle rests where the draw starts,
aim holds where the draw ends and where the release begins. That is what lets the
AnimBlueprint switch states without a crossfade papering over a step.

The string is the reason this rig exists. Its vertices are blended linearly
between ``string_mid`` and the tip bone on their side, which turns a straight
rest string into a clean V the moment ``string_mid`` moves back -- no per-frame
geometry rebuild, no spline component.

Coordinate convention (matches ``create_round_body_character.py``): +Z is up and
Blender -Y is the direction the character faces, which exports as Unreal +X. The
bow therefore stands along Z, the riser sits at Y=0 (nearest the target) and the
string hangs at +Y, on the archer's side. That is a real archer's grip: draw the
string toward +Y and the arrow leaves toward -Y.

Keeping the draw in a clip is what shrinks the hand-authored part of this pipeline to
almost nothing: the AnimBlueprint samples an asset instead of carrying per-bone transform
math that no script can author or check.

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

# --- Draw and release feel (ported from SkyLand's RenderWeaponDraw.ts) ------
# These are the numbers that decide how the bow feels, and they now live in exactly one
# place: the clips below are generated from them, so tuning the feel is editing a constant
# and re-running, not hand-moving keyframes that no longer match anything.
#: How far back each limb tip rotates at full draw, radians (16 degrees).
BOW_LIMB_BEND_RADIANS = math.radians(16.0)
#: How far the string's midpoint travels toward the archer at full draw, metres.
BOW_STRING_PULL = 0.18
#: How long the loose takes, seconds.
BOW_RELEASE_SECONDS = 0.12
#: How far the string overshoots past rest before settling, metres.
RELEASE_OVERSHOOT = 0.03
#: Damping on the overshoot. Higher settles sooner.
RELEASE_DAMPING = 12.0
#: How many times the string crosses rest while settling.
RELEASE_WOBBLES = 2
#: The string returns to rest over this fraction of the loose, then wobbles.
RELEASE_RETURN_RATIO = 0.3
#: The limbs straighten over this fraction of the loose -- sooner than the string,
#: because wood is stiffer than a cord and the two settling together reads as rubber.
LIMB_RECOVER_RATIO = 1.0 / 3.0

# --- Idle and aim loops -----------------------------------------------------
# The two states the bow spends most of its screen time in, and the two that were
# missing: carried at rest, and held at full draw waiting for the shot. A bow that
# freezes on a single pose between shots reads as a prop, not a weapon.
# Both clips loop, so every curve below runs a whole number of cycles over its own
# length and the last key lands exactly back on the first.
#: How long one idle breath takes, seconds.
BOW_IDLE_SECONDS = 2.4
#: Breathing cycles inside one idle loop.
IDLE_BREATH_CYCLES = 1
#: How far the braced string drifts with the archer's breathing, metres. Small on
#: purpose: this is a strung bow being carried, not a plucked one.
IDLE_STRING_SWAY = 0.004
#: How far the limbs flex with that breath, radians (0.35 degrees).
IDLE_LIMB_SWAY = math.radians(0.35)
#: How long one aim tremble cycle set takes, seconds.
BOW_AIM_SECONDS = 1.6
#: Tremble cycles inside one aim loop. Faster than breathing, because this is the
#: strain of holding a drawn bow rather than the breath underneath it.
AIM_TREMBLE_CYCLES = 3
#: How far the held string trembles either side of full draw, metres.
AIM_STRING_TREMBLE = 0.006
#: How far the limbs answer that tremble, radians (0.5 degrees).
AIM_LIMB_TREMBLE = math.radians(0.5)
#: Keys per second for the two loops. Their curves are slow sines, so keying them at
#: the release's rate would multiply keys without adding motion. Must divide
#: ANIMATION_FPS: the loops are keyed on the same frame ruler everything else uses.
LOOP_KEY_FPS = 30

IDLE_ACTION_NAME = "WoodBow_Idle"
DRAW_ACTION_NAME = "WoodBow_Draw"
AIM_ACTION_NAME = "WoodBow_Aim"
RELEASE_ACTION_NAME = "WoodBow_Release"
#: Every Action this script owns. The re-run cleanup deletes exactly these by name, so a
#: clip added above must be added here too -- miss one and its fake user keeps the old
#: Action alive, the next run bakes WoodBow_Idle.001 beside it, and the export can pick
#: the stale one.
CLIP_ACTION_NAMES = (IDLE_ACTION_NAME, DRAW_ACTION_NAME, AIM_ACTION_NAME, RELEASE_ACTION_NAME)

#: One FBX holding every clip as its own animation stack. Blender names each stack after
#: the Action, so the four clips stay distinguishable inside the single file.
CLIPS_FBX_NAME = "A_WoodBow_Clips.fbx"

#: Clip lengths in seconds, as a plain literal the Unreal import script reads with
#: ast.literal_eval to check the timing survived the trip. It restates numbers derived
#: above, so verify_clip_contract() below asserts the two agree rather than trusting that
#: whoever edits a feel constant remembers to edit this table too.
#: Release is 0.125s, not the 0.12s BOW_RELEASE_SECONDS asks for: 0.12 * 120fps is 14.4
#: frames and a clip cannot end mid-frame, so the bake rounds up to 15. These are the
#: lengths as baked, which is what Unreal measures on the imported asset.
CLIP_SECONDS = {
    "WoodBow_Idle": 2.4,
    "WoodBow_Draw": 0.2,
    "WoodBow_Aim": 1.6,
    "WoodBow_Release": 0.125,
}

#: True: one FBX, one animation stack per clip (what content uses).
#: False: one FBX per clip, the older layout. Kept because Unreal's importer decides how
#: many stacks it will take out of a single file, and that is a property of the engine
#: build rather than of this script -- if UE ever brings back only the first stack, this
#: is the one switch that gets the pipeline working again.
BUNDLE_CLIPS_IN_ONE_FBX = True

IDLE_FBX_NAME = "A_WoodBow_Idle.fbx"
DRAW_FBX_NAME = "A_WoodBow_Draw.fbx"
AIM_FBX_NAME = "A_WoodBow_Aim.fbx"
RELEASE_FBX_NAME = "A_WoodBow_Release.fbx"
#: The draw clip is sampled by charge, never played, so its length is just a 0..1 ruler.
DRAW_FRAME_COUNT = 24
#: 120fps, not 60. The release crosses rest RELEASE_WOBBLES times inside 0.12s -- about
#: 17Hz -- and 60fps leaves under four samples per cycle, which aliases the wobble into
#: a stutter. Unreal resamples the imported clip; the cost here is a dozen extra keys.
ANIMATION_FPS = 120
#: Bones the clips write. Everything else follows through the hierarchy.
POSED_BONES = ("limb_upper", "limb_lower", "string_mid")

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


# --- Draw and release curves ------------------------------------------------
# Pure functions of charge or elapsed time: no bpy, no bones, no scene. They are the
# single definition of the feel, sampled below to bake the clips and checkable on their
# own without Blender.


def clamp01(value):
    return max(0.0, min(1.0, value))


def draw_string_pull(charge):
    """How far back the string's midpoint sits at *charge* (0..1)."""
    return clamp01(charge) * BOW_STRING_PULL


def draw_limb_bend(charge):
    """How far back each limb tip has swung at *charge* (0..1)."""
    return clamp01(charge) * BOW_LIMB_BEND_RADIANS


def release_string_pull(start_pull, elapsed_seconds):
    """Where the string's midpoint is *elapsed_seconds* after the loose.

    Returning to rest and wobbling are two separate terms on purpose. Folded into one
    decay curve, the return swallows the overshoot and the string merely sags back --
    there is no snap. The wobble is allowed to go negative: that is the string passing
    rest and springing forward, which is the whole recognisable part of a bow firing.
    """
    progress = clamp01(elapsed_seconds / BOW_RELEASE_SECONDS)
    snap_back = start_pull * max(0.0, 1.0 - progress / RELEASE_RETURN_RATIO)
    wobble = (
        -RELEASE_OVERSHOOT
        * math.sin(2.0 * math.pi * RELEASE_WOBBLES * progress)
        * math.exp(-RELEASE_DAMPING * elapsed_seconds)
    )
    return snap_back + wobble


def release_limb_bend(start_bend, elapsed_seconds):
    """How far back the limb tips still are *elapsed_seconds* after the loose."""
    progress = elapsed_seconds / BOW_RELEASE_SECONDS
    return start_bend * max(0.0, 1.0 - progress / LIMB_RECOVER_RATIO)


def draw_samples():
    """Two keys. Linear between them, because Unreal samples this clip by charge."""
    return [
        (0.0, draw_limb_bend(0.0), draw_string_pull(0.0)),
        (float(DRAW_FRAME_COUNT), draw_limb_bend(1.0), draw_string_pull(1.0)),
    ]


def idle_string_pull(elapsed_seconds):
    """Where the braced string sits *elapsed_seconds* into the idle loop.

    Raised cosine rather than a sine: it starts and ends at exactly zero, and it never
    goes negative. A negative value here would mean the string bowing *away* from the
    archer, past its own brace height, which is the one place a strung cord cannot go.
    """
    phase = 2.0 * math.pi * IDLE_BREATH_CYCLES * elapsed_seconds / BOW_IDLE_SECONDS
    return IDLE_STRING_SWAY * 0.5 * (1.0 - math.cos(phase))


def idle_limb_bend(elapsed_seconds):
    """How far the limbs flex with the idle breath, in phase with the string."""
    phase = 2.0 * math.pi * IDLE_BREATH_CYCLES * elapsed_seconds / BOW_IDLE_SECONDS
    return IDLE_LIMB_SWAY * 0.5 * (1.0 - math.cos(phase))


def aim_string_pull(elapsed_seconds):
    """Where the held string sits *elapsed_seconds* into the aim loop.

    Centred on full draw and symmetric, so frame 0 of this clip is the pose the draw
    ramp ends on and the pose the release starts from. Blending into or out of the hold
    therefore costs nothing -- there is no step to hide behind a crossfade.
    """
    phase = 2.0 * math.pi * AIM_TREMBLE_CYCLES * elapsed_seconds / BOW_AIM_SECONDS
    return BOW_STRING_PULL + AIM_STRING_TREMBLE * math.sin(phase)


def aim_limb_bend(elapsed_seconds):
    """How far back the limb tips sit while the draw is held."""
    phase = 2.0 * math.pi * AIM_TREMBLE_CYCLES * elapsed_seconds / BOW_AIM_SECONDS
    return BOW_LIMB_BEND_RADIANS + AIM_LIMB_TREMBLE * math.sin(phase)


def loop_key_step():
    """Frames between keys on the looping clips, on the ANIMATION_FPS ruler."""
    if ANIMATION_FPS % LOOP_KEY_FPS != 0:
        raise RuntimeError(
            f"LOOP_KEY_FPS={LOOP_KEY_FPS} must divide ANIMATION_FPS={ANIMATION_FPS}, or the "
            "loops cannot close on a whole key."
        )
    return ANIMATION_FPS // LOOP_KEY_FPS


def loop_frame_count(seconds):
    frames = seconds * ANIMATION_FPS
    total = int(round(frames))
    step = loop_key_step()
    if abs(frames - total) > 1e-6 or total % step != 0:
        raise RuntimeError(
            f"A {seconds}s loop is {frames} frames at {ANIMATION_FPS}fps, which is not a whole "
            f"multiple of the {step}-frame key step. Pick a length that is."
        )
    return total


def loop_samples(seconds, limb_bend_at, string_pull_at_time):
    """Sample a looping clip across one whole period, inclusive of both ends.

    The final key is restated from t=0 rather than evaluated at t=period. The two are
    equal in algebra, but not in floating point, and a loop that ends a micrometre off
    where it began ticks once per cycle forever.
    """
    total = loop_frame_count(seconds)
    step = loop_key_step()
    samples = [
        (float(frame), limb_bend_at(frame / ANIMATION_FPS), string_pull_at_time(frame / ANIMATION_FPS))
        for frame in range(0, total, step)
    ]
    samples.append((float(total), limb_bend_at(0.0), string_pull_at_time(0.0)))
    return samples


def idle_samples():
    return loop_samples(BOW_IDLE_SECONDS, idle_limb_bend, idle_string_pull)


def aim_samples():
    return loop_samples(BOW_AIM_SECONDS, aim_limb_bend, aim_string_pull)


def release_frame_count():
    return int(math.ceil(BOW_RELEASE_SECONDS * ANIMATION_FPS))


def release_samples():
    """Every frame, so the wobble survives. Starts exactly where the draw ended."""
    samples = []
    for frame in range(release_frame_count() + 1):
        elapsed = frame / ANIMATION_FPS
        samples.append(
            (
                float(frame),
                release_limb_bend(BOW_LIMB_BEND_RADIANS, elapsed),
                release_string_pull(BOW_STRING_PULL, elapsed),
            )
        )
    return samples


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
    # The FBX exporter times baked animation off the scene rate, so this has to be set
    # before the clips are baked, not just before they are exported.
    scene.render.fps = ANIMATION_FPS
    scene.render.fps_base = 1.0
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"


def remove_generated_collection():
    """Delete only data previously owned by this script."""
    for action in list(bpy.data.actions):
        # Fake users keep these alive across saves, so a re-run has to clear them by name
        # or Blender hands out WoodBow_Draw.001 and the export picks the stale one. The
        # suffix is stripped on purpose: one bundled FBX exports *every* Action the rig can
        # play, so a leftover .001 would ride along as a fifth stack.
        if action.name.split(".")[0] in CLIP_ACTION_NAMES:
            bpy.data.actions.remove(action)

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


def apply_bow_pose(rig, limb_bend, string_pull):
    """Put the rig at one point on the draw: limbs swung back, string pulled in.

    Written as direct property assignment rather than pose-mode operators so it behaves
    identically under `blender --background`, where operator context is a common source
    of silent no-ops.
    """
    for bone_name, sign in (("limb_upper", 1.0), ("limb_lower", -1.0)):
        pose_bone = rig.pose.bones[bone_name]
        pose_bone.rotation_mode = "QUATERNION"
        # Swing the tip toward the archer about world X. The lower limb mirrors it,
        # the same sign flip SkyLand applies to its two pivot groups.
        pose_bone.rotation_quaternion = mathutils.Quaternion(
            to_bone_space(pose_bone, (1.0, 0.0, 0.0)), sign * limb_bend
        )
    # Draw the string toward the archer, +Y in world space.
    string_mid = rig.pose.bones["string_mid"]
    string_mid.location = to_bone_space(string_mid, (0.0, string_pull, 0.0))
    bpy.context.view_layer.update()


def clear_bow_pose(rig):
    apply_bow_pose(rig, 0.0, 0.0)


def string_pull_at(rig, brace_height):
    """How far back the string's midpoint bone currently sits, in metres."""
    return rig.pose.bones["string_mid"].head.y - brace_height


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

    try:
        apply_bow_pose(rig, BOW_LIMB_BEND_RADIANS, BOW_STRING_PULL)

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
        clear_bow_pose(rig)


# --- Clips ------------------------------------------------------------------


def new_action_slot(action, rig):
    """Create a slot for *rig* on *action*, across the signatures 4.4..5.x have shipped."""
    attempts = (
        lambda: action.slots.new(id_type="OBJECT", name=rig.name),
        lambda: action.slots.new("OBJECT", rig.name),
        lambda: action.slots.new(),
    )
    for attempt in attempts:
        try:
            return attempt()
        except (TypeError, RuntimeError):
            continue
    return None


def bind_action_slot(rig, action):
    """Bind the Action's slot to the rig, so keys have somewhere to land.

    Blender 4.4 moved an Action's channels behind a *slot*: the Action holds layers, a
    layer holds strips, and a strip holds one channel bag per slot. Assigning the Action
    to an object normally binds a slot by itself, but when it does not, keyframe_insert
    still reports success and writes nothing -- ledger PY-BLENDER-002. Returns the bound
    slot, or None on a pre-4.4 build where channels hang straight off the Action.
    """
    animation_data = rig.animation_data
    if getattr(action, "slots", None) is None:
        return None
    slot = getattr(animation_data, "action_slot", None)
    # Only reuse a bound slot that belongs to *this* Action. Four clips are baked in a
    # row through the same rig, and a slot left bound from the previous one would look
    # like "already bound" while this Action still has nowhere to put its keys.
    if slot is not None and any(existing == slot for existing in action.slots):
        return slot
    slot = action.slots[0] if len(action.slots) else new_action_slot(action, rig)
    if slot is not None:
        animation_data.action_slot = slot
    return slot


def assign_action(rig, action):
    """Put *action* on the rig and bind its slot. The only way this file assigns one.

    Baking, playback validation and export each need the rig actually driven by the clip.
    Written out at each site, the slot binding gets forgotten at one of them, and a
    missed binding does not raise -- the rig simply sits at rest while the validator
    reports the clip is flat, which sends you looking at the curves instead.
    """
    rig.animation_data.action = action
    return bind_action_slot(rig, action)


def get_fcurves(action):
    """Every F-Curve in *action*, on both the legacy and the slotted Action API.

    Same name and shape as the helper in boiler_animation.py / door_animation.py and the
    cookbook in claude-blender.md, so it greps as one thing; it differs in raising rather
    than returning [] on an unknown API, because here an empty result is also what a clip
    that failed to bake looks like.

    Blender 5.x dropped legacy Actions, and with them ``Action.fcurves`` -- reading it
    raises AttributeError rather than returning empty (ledger PY-BLENDER-002). Channels
    now live in ``action.layers[].strips[].channelbags[]``. Both shapes are read here so
    the rest of the script can keep asking one question: what did this clip key?
    """
    legacy = getattr(action, "fcurves", None)
    if legacy is not None:
        return list(legacy)

    layers = getattr(action, "layers", None)
    if layers is None:
        raise RuntimeError(
            f"{action.name} exposes neither 'fcurves' nor 'layers'. This Blender build's "
            "Action API is neither the legacy nor the slotted one; update get_fcurves()."
        )

    curves = []
    for layer in layers:
        for strip in layer.strips:
            for channelbag in getattr(strip, "channelbags", ()):
                curves.extend(channelbag.fcurves)
    return curves


def action_api_name(action):
    """Which Action API this build uses. Only ever appears in failure messages."""
    return "legacy Action.fcurves" if getattr(action, "fcurves", None) is not None else (
        "slotted layers/strips/channelbags"
    )


def verify_clip_contract():
    """Check CLIP_SECONDS still matches the constants the clips are generated from.

    That table exists only because the Unreal script cannot evaluate this file, just read
    literals out of it. Restated numbers drift, and this drift would be quiet: Unreal would
    check each imported clip against a length no clip has had for months and pass.
    """
    derived = {
        IDLE_ACTION_NAME: BOW_IDLE_SECONDS,
        DRAW_ACTION_NAME: DRAW_FRAME_COUNT / ANIMATION_FPS,
        AIM_ACTION_NAME: BOW_AIM_SECONDS,
        RELEASE_ACTION_NAME: release_frame_count() / ANIMATION_FPS,
    }
    wrong = {
        name: (CLIP_SECONDS.get(name), seconds)
        for name, seconds in derived.items()
        if abs(CLIP_SECONDS.get(name, -1.0) - seconds) > 1e-9
    }
    if wrong or set(CLIP_SECONDS) != set(derived):
        raise RuntimeError(
            f"CLIP_SECONDS disagrees with the feel constants: {wrong or CLIP_SECONDS}. "
            "Update the table at the top of this file; the Unreal import script reads it."
        )


def bake_action(rig, action_name, samples):
    """Key the bow's three driven bones across *samples* into a fresh Action.

    Samples are ``(frame, limb_bend, string_pull)``. Keys are LINEAR: the draw is
    sampled by charge and the release is baked every frame, so in both cases a Bezier
    handle would only invent motion that the curves above never described.
    """
    if rig.animation_data is None:
        rig.animation_data_create()

    action = bpy.data.actions.new(action_name)
    action.use_fake_user = True
    slot = assign_action(rig, action)

    for frame, limb_bend, string_pull in samples:
        bpy.context.scene.frame_set(int(round(frame)))
        apply_bow_pose(rig, limb_bend, string_pull)
        for bone_name in ("limb_upper", "limb_lower"):
            rig.pose.bones[bone_name].keyframe_insert("rotation_quaternion", frame=frame)
        rig.pose.bones["string_mid"].keyframe_insert("location", frame=frame)

    # Blender 4.4+ routes an Action's channels through a slot. A fresh Action has none
    # until something binds one, and if that binding does not happen the keyframe_insert
    # calls above report success while the Action stays empty -- which would export as a
    # clip that simply does nothing. Cheaper to notice here than in Unreal.
    curves = get_fcurves(action)
    if not curves:
        raise RuntimeError(
            f"{action_name} has no F-Curves after keying {len(samples)} samples, read "
            f"through the {action_api_name(action)} API with slot "
            f"{getattr(slot, 'name', None)!r}. The Action was never bound to the rig."
        )

    for fcurve in curves:
        for keyframe in fcurve.keyframe_points:
            keyframe.interpolation = "LINEAR"
    return action


def validate_clips(rig, draw_action, release_action, brace_height):
    """Play the clips back through the rig and check they say what the curves said.

    Baking is where the feel can quietly disappear: a stray simplify factor, a scene
    frame rate that resamples the wobble away, an Action that got keyed while the pose
    was already cleared. None of that raises -- the clip just goes flat, and a flat
    release looks like a string sagging home instead of snapping. So read the poses back.
    """
    require_channels = {"limb_upper", "limb_lower", "string_mid"}
    for action in (draw_action, release_action):
        keyed = {
            fcurve.data_path.split('"')[1]
            for fcurve in get_fcurves(action)
            if '"' in fcurve.data_path
        }
        missing = require_channels - keyed
        if missing:
            raise RuntimeError(f"{action.name} has no keys for {sorted(missing)}")

    assign_action(rig, draw_action)
    bpy.context.scene.frame_set(0)
    bpy.context.view_layer.update()
    at_rest = string_pull_at(rig, brace_height)
    bpy.context.scene.frame_set(DRAW_FRAME_COUNT)
    bpy.context.view_layer.update()
    at_full = string_pull_at(rig, brace_height)
    if abs(at_rest) > 1e-4 or abs(at_full - BOW_STRING_PULL) > 1e-4:
        raise RuntimeError(
            f"{draw_action.name} runs {at_rest:.4f}m -> {at_full:.4f}m, expected "
            f"0 -> {BOW_STRING_PULL:.4f}m"
        )

    assign_action(rig, release_action)
    played = []
    for frame in range(release_frame_count() + 1):
        bpy.context.scene.frame_set(frame)
        bpy.context.view_layer.update()
        played.append(string_pull_at(rig, brace_height))

    # The loose has to start exactly where the draw ended, or the string teleports on
    # the frame the arrow leaves -- the one frame the player is certain to be watching.
    if abs(played[0] - at_full) > 1e-4:
        raise RuntimeError(
            f"{release_action.name} starts at {played[0]:.4f}m but the draw ends at "
            f"{at_full:.4f}m; the string would jump when the arrow leaves"
        )
    if abs(played[-1]) > 1e-3:
        raise RuntimeError(
            f"{release_action.name} ends {played[-1]:.4f}m from rest; the string never "
            "settles"
        )
    if min(played) > -RELEASE_OVERSHOOT * 0.2:
        raise RuntimeError(
            f"{release_action.name} never springs past rest (closest {min(played):.4f}m, "
            f"expected about {-RELEASE_OVERSHOOT:.4f}m). The wobble was resampled away -- "
            "check ANIMATION_FPS and that no simplify factor is applied on export."
        )
    return played


def validate_loop_clip(rig, action, seconds, base_pull, amplitude, brace_height):
    """Play a looping clip back and check it loops, and that it moves while it does.

    Two failures this catches, both of which export cleanly and neither of which raises:
    a loop whose last key drifted off its first, which ticks once every cycle for as long
    as the bow is on screen; and a loop that baked flat, which is indistinguishable from
    the missing clip it was written to replace.
    """
    total = loop_frame_count(seconds)
    assign_action(rig, action)
    played = []
    for frame in range(0, total + 1, loop_key_step()):
        bpy.context.scene.frame_set(frame)
        bpy.context.view_layer.update()
        played.append(string_pull_at(rig, brace_height))

    if abs(played[0] - base_pull) > max(1e-4, amplitude * 0.05):
        raise RuntimeError(
            f"{action.name} starts at {played[0]:.5f}m, expected {base_pull:.5f}m. Its first "
            "frame is the pose the neighbouring clips blend from, so it cannot drift."
        )
    if abs(played[-1] - played[0]) > 1e-5:
        raise RuntimeError(
            f"{action.name} ends {played[-1] - played[0]:+.6f}m away from where it started; "
            "the loop would tick once per cycle."
        )
    travel = max(played) - min(played)
    if travel < amplitude * 0.5:
        raise RuntimeError(
            f"{action.name} only moves {travel:.5f}m across its loop, expected about "
            f"{amplitude:.5f}m. The clip baked flat -- check that the pose was applied before "
            "each key rather than after."
        )
    return played


def validate_state_clips(rig, idle_action, aim_action, draw_action, brace_height):
    """Check the four clips join up into idle -> draw -> aim -> release without a step.

    Each clip is correct on its own and the set can still be wrong: what the player sees
    at a state change is the seam, and a seam is a property of the pair, not of either
    clip. So the ends are compared here, once, rather than trusted four times.
    """
    idle_played = validate_loop_clip(
        rig, idle_action, BOW_IDLE_SECONDS, 0.0, IDLE_STRING_SWAY, brace_height
    )
    if min(idle_played) < -1e-4 or max(idle_played) > IDLE_STRING_SWAY * 1.5:
        raise RuntimeError(
            f"{idle_action.name} ranges {min(idle_played):+.5f}m..{max(idle_played):+.5f}m; a "
            f"braced string may only breathe forward, between 0 and {IDLE_STRING_SWAY:.5f}m."
        )
    aim_played = validate_loop_clip(
        rig, aim_action, BOW_AIM_SECONDS, BOW_STRING_PULL, AIM_STRING_TREMBLE * 2.0, brace_height
    )

    # Idle's rest pose is the draw's first frame, and the hold is the draw's last.
    assign_action(rig, draw_action)
    bpy.context.scene.frame_set(0)
    bpy.context.view_layer.update()
    draw_start = string_pull_at(rig, brace_height)
    bpy.context.scene.frame_set(DRAW_FRAME_COUNT)
    bpy.context.view_layer.update()
    draw_end = string_pull_at(rig, brace_height)
    if abs(idle_played[0] - draw_start) > 1e-4:
        raise RuntimeError(
            f"{idle_action.name} rests at {idle_played[0]:.5f}m but {draw_action.name} starts "
            f"at {draw_start:.5f}m; the string would jump the moment the player pulls."
        )
    if abs(aim_played[0] - draw_end) > 1e-4:
        raise RuntimeError(
            f"{aim_action.name} holds at {aim_played[0]:.5f}m but {draw_action.name} ends at "
            f"{draw_end:.5f}m; the string would jump the moment the draw completes."
        )
    return idle_played, aim_played


def export_action_fbx(rig, action, file_name, frame_end):
    """One FBX per clip: armature only, no mesh. The Unreal import pairs it with SK_WoodBow."""
    output_dir = find_project_root() / "blender" / "models"
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / file_name

    assign_action(rig, action)
    scene = bpy.context.scene
    scene.frame_start = 0
    scene.frame_end = int(frame_end)

    bpy.ops.object.select_all(action="DESELECT")
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.fbx(
        filepath=str(output_path),
        object_types={"ARMATURE"},
        bake_anim=True,
        bake_anim_use_all_bones=True,
        bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False,
        bake_anim_force_startend_keying=True,
        bake_anim_step=1.0,
        # Zero, deliberately. Any simplification here eats the release wobble, which is
        # a handful of frames of small motion and exactly what a curve fitter discards.
        bake_anim_simplify_factor=0.0,
        **UE_FBX_COMMON,
    )

    if not output_path.is_file() or output_path.stat().st_size < 1024:
        raise RuntimeError(f"Animation FBX is missing or unexpectedly small: {output_path}")
    return output_path


def export_clips_fbx(rig, actions):
    """Every clip in one FBX, each Action exported as its own animation stack.

    One file is the right shape for this asset: a single prop whose clips all drive the
    same three bones, authored together and revised together. Splitting them across four
    files only pays off when clips are authored separately or by different people.

    ``bake_anim_use_all_actions`` exports *every* Action the rig can play, not a list this
    function chooses, which is why the cleanup above deletes leftovers by stripped name --
    a stray WoodBow_Idle.001 would ride along as a fifth stack and Unreal would import it.
    """
    output_dir = find_project_root() / "blender" / "models"
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / CLIPS_FBX_NAME

    assign_action(rig, actions[0])
    scene = bpy.context.scene
    scene.frame_start = 0
    scene.frame_end = max(
        int(round(max(point.co[0] for curve in get_fcurves(action) for point in curve.keyframe_points)))
        for action in actions
    )

    bpy.ops.object.select_all(action="DESELECT")
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.fbx(
        filepath=str(output_path),
        object_types={"ARMATURE"},
        bake_anim=True,
        bake_anim_use_all_bones=True,
        bake_anim_use_nla_strips=False,
        # The one line that makes this a bundle rather than a single-clip file.
        bake_anim_use_all_actions=True,
        bake_anim_force_startend_keying=True,
        bake_anim_step=1.0,
        # Zero, deliberately. Any simplification here eats the release wobble, which is
        # a handful of frames of small motion and exactly what a curve fitter discards.
        bake_anim_simplify_factor=0.0,
        **UE_FBX_COMMON,
    )

    if not output_path.is_file() or output_path.stat().st_size < 4096:
        raise RuntimeError(f"Clip bundle is missing or unexpectedly small: {output_path}")
    verify_bundle_contains(output_path, [action.name for action in actions])
    return output_path


def verify_bundle_contains(output_path, action_names):
    """Confirm each clip really made it into the bundle.

    A bundle that silently carries one stack looks exactly like a healthy export from
    here: the file exists, it is the right sort of size, the exporter raised nothing. FBX
    stores stack names as plain ASCII, so the cheapest honest check is to look for them.
    """
    data = output_path.read_bytes()
    missing = [name for name in action_names if name.encode("ascii") not in data]
    if missing:
        raise RuntimeError(
            f"{output_path.name} does not contain {missing}. Blender exported "
            f"{len(action_names) - len(missing)} of {len(action_names)} stacks; check that "
            "every clip is still an Action with a fake user at export time."
        )


#: Settings both exports must agree on. Split into two literals they drift, and a bone
#: set or axis that differs between the mesh FBX and an animation FBX is rejected by
#: Unreal as "skeleton does not match" long after anyone remembers editing one of them.
UE_FBX_COMMON = dict(
    use_selection=True,
    global_scale=1.0,
    apply_unit_scale=True,
    apply_scale_options="FBX_SCALE_UNITS",
    use_space_transform=True,
    bake_space_transform=False,
    axis_forward="-Y",
    axis_up="Z",
    add_leaf_bones=False,
    primary_bone_axis="Y",
    secondary_bone_axis="X",
    use_armature_deform_only=True,
    armature_nodetype="NULL",
    path_mode="AUTO",
    embed_textures=False,
)


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
        object_types={"ARMATURE", "MESH"},
        use_mesh_modifiers=True,
        mesh_smooth_type="FACE",
        use_tspace=True,
        bake_anim=False,
        **UE_FBX_COMMON,
    )

    if not output_path.is_file() or output_path.stat().st_size < 4096:
        raise RuntimeError(f"FBX export is missing or unexpectedly small: {output_path}")
    return output_path


def build_wood_bow():
    verify_clip_contract()
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

    # The mesh FBX goes out first, while the rig still has no animation data at all --
    # Unreal reads the skeletal mesh's bind pose from it, and a posed or keyed armature
    # here is how a bow ends up permanently half-drawn in the content browser.
    output_path = export_ue5_fbx(mesh, rig)

    idle_action = bake_action(rig, IDLE_ACTION_NAME, idle_samples())
    draw_action = bake_action(rig, DRAW_ACTION_NAME, draw_samples())
    aim_action = bake_action(rig, AIM_ACTION_NAME, aim_samples())
    release_action = bake_action(rig, RELEASE_ACTION_NAME, release_samples())
    played = validate_clips(rig, draw_action, release_action, BOW_BRACE_HEIGHT)
    idle_played, aim_played = validate_state_clips(
        rig, idle_action, aim_action, draw_action, BOW_BRACE_HEIGHT
    )
    clip_actions = [idle_action, draw_action, aim_action, release_action]
    if BUNDLE_CLIPS_IN_ONE_FBX:
        clip_paths = [export_clips_fbx(rig, clip_actions)]
    else:
        clip_paths = [
            export_action_fbx(rig, idle_action, IDLE_FBX_NAME, loop_frame_count(BOW_IDLE_SECONDS)),
            export_action_fbx(rig, draw_action, DRAW_FBX_NAME, DRAW_FRAME_COUNT),
            export_action_fbx(rig, aim_action, AIM_FBX_NAME, loop_frame_count(BOW_AIM_SECONDS)),
            export_action_fbx(rig, release_action, RELEASE_FBX_NAME, release_frame_count()),
        ]

    # Leave the .blend in a state you can actually look at. Clearing the assignment (what
    # this did before) left the Action Editor showing "New" on a rig whose four clips were
    # sitting right there in the file -- indistinguishable, from the UI, from a run that
    # baked nothing. Idle's frame 0 *is* the braced rest pose, so parking there also keeps
    # the old promise of not saving a half-drawn bow.
    assign_action(rig, idle_action)
    scene = bpy.context.scene
    scene.frame_start = 0
    scene.frame_end = loop_frame_count(BOW_IDLE_SECONDS)
    scene.frame_set(0)

    print(f"Wood bow created in collection: {COLLECTION_NAME}")
    print(f"Skeletal mesh: {MESH_NAME}  ({len(mesh.data.vertices)} verts)")
    print(f"Bones: {', '.join(EXPECTED_BONES)}")
    print(f"Limb arc solved: radius={radius:.4f}m half_arc={math.degrees(half_arc):.2f}deg")
    print(
        f"Draw pose verified: string_mid +Y {BOW_STRING_PULL}m, limbs "
        f"{math.degrees(BOW_LIMB_BEND_RADIANS):.1f}deg"
    )
    print(
        f"Clips baked at {ANIMATION_FPS}fps: {IDLE_ACTION_NAME} "
        f"({BOW_IDLE_SECONDS}s loop), {DRAW_ACTION_NAME} "
        f"({DRAW_FRAME_COUNT} frames, sample it by charge), {AIM_ACTION_NAME} "
        f"({BOW_AIM_SECONDS}s loop), {RELEASE_ACTION_NAME} "
        f"({release_frame_count()} frames, play it by time)"
    )
    print(
        f"Idle verified: breathes 0 -> {max(idle_played):+.5f}m and closes its loop; "
        f"aim verified: holds {min(aim_played):+.5f}m..{max(aim_played):+.5f}m around full draw"
    )
    print(
        f"Release verified: springs to {min(played):+.4f}m past rest, settles at "
        f"{played[-1]:+.4f}m"
    )
    print("Attach the arrow to an Unreal Skeleton socket named 'nock' on string_mid.")
    print(f"UE5 skeletal FBX exported: {output_path}")
    print(f"FBX size: {output_path.stat().st_size // 1024} KB")
    print("Actions in this .blend -- switch them in Dope Sheet > Action Editor:")
    for action, end_frame in (
        (idle_action, loop_frame_count(BOW_IDLE_SECONDS)),
        (draw_action, DRAW_FRAME_COUNT),
        (aim_action, loop_frame_count(BOW_AIM_SECONDS)),
        (release_action, release_frame_count()),
    ):
        print(
            f"  {action.name:16} frames 0..{end_frame:<4} {end_frame / ANIMATION_FPS:.2f}s  "
            f"{len(get_fcurves(action))} curves"
        )
    print(
        f"{IDLE_ACTION_NAME} is loaded on {RIG_NAME} and the frame range is set to it; "
        "press Space to watch it loop."
    )
    for clip_path in clip_paths:
        print(f"Clips exported: {clip_path} ({clip_path.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    build_wood_bow()
