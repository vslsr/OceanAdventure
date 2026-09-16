"""List the wood bow's clips, measure them, and load one for playback.

Run this from Blender's Scripting workspace after ``create_wood_bow.py``. It changes
nothing on disk and generates nothing: it answers two questions the Blender UI answers
badly, and then hands you a clip to watch.

    Are the clips even in this file?
        The Outliner's "Animation" entry under an object shows only the clip currently
        assigned, so three of four look missing. Actions live in the file, not on the
        object; this prints all of them.

    Is the clip actually moving anything?
        The idle breath travels 20mm on a 1.05m bow. That is visible when you know where
        to look and invisible when you do not, so "nothing is moving" and "something is
        moving a little" are the same picture. This plays each clip through the rig and
        measures how far the string's midpoint travels, in millimetres.

Set CLIP below to the one you want loaded, then run. Self-contained on purpose: pasted
into Blender's Text Editor this file may have no path on disk and no project root to find,
so it imports nothing from the repo.

Where the report shows up: as a popup, and as a Text datablock named ``WoodBow_Report``
you can open in the Text Editor. Not only through ``print()`` -- that goes to the *system*
console, hidden by default on Windows (Window > Toggle System Console), and neither the
Python Console nor the Info editor ever shows it. A script whose only output is a print is
a script that, from the UI, did nothing.
"""

import bpy


#: Where the report is left behind, openable in Blender's Text Editor.
REPORT_TEXT_NAME = "WoodBow_Report"
#: The clip to leave loaded on the rig. One of the names this prints.
CLIP = "WoodBow_Idle"
RIG_NAME = "SKEL_WoodBow"
#: Clips are named after the rig they drive, so this finds them without a hard-coded list.
CLIP_PREFIX = "WoodBow_"
#: The bone the draw and both loops actually translate. Its travel is the clip's motion.
MEASURED_BONE = "string_mid"


def bind_action_slot(rig, action):
    """Bind the Action's own slot, the way create_wood_bow.py does.

    Blender 4.4+ keeps channels in a per-slot bag, and the rig plays only the bag its
    bound slot names. Assigning an Action from Python without binding its slot leaves the
    Action Editor showing the clip with no channels under it, and nothing moves.
    """
    if getattr(action, "slots", None) is None or not len(action.slots):
        return None
    named = [slot for slot in action.slots if rig.name in getattr(slot, "name", "")]
    slot = (named or list(action.slots))[0]
    rig.animation_data.action_slot = slot
    return slot


def get_fcurves(action):
    """Channels of *action* on both the legacy (<=4.3) and slotted (4.4+) API."""
    legacy = getattr(action, "fcurves", None)
    if legacy is not None:
        return list(legacy)
    return [
        curve
        for layer in getattr(action, "layers", ())
        for strip in layer.strips
        for bag in getattr(strip, "channelbags", ())
        for curve in bag.fcurves
    ]


def key_span(action):
    """First and last keyed frame across every channel, or None on an empty Action."""
    frames = [point.co[0] for curve in get_fcurves(action) for point in curve.keyframe_points]
    return (min(frames), max(frames)) if frames else None


def measure(rig, action, span):
    """Play *action* through the rig and report how far the measured bone travels, in mm."""
    rig.animation_data.action = action
    bind_action_slot(rig, action)
    first, last = int(round(span[0])), int(round(span[1]))
    positions = []
    for frame in range(first, last + 1, max(1, (last - first) // 24 or 1)):
        bpy.context.scene.frame_set(frame)
        bpy.context.view_layer.update()
        positions.append(rig.pose.bones[MEASURED_BONE].head.y)
    return (max(positions) - min(positions)) * 1000.0


def report(lines):
    """Print *lines*, leave them in a Text datablock, and pop them on screen."""
    for line in lines:
        print(line)

    text = bpy.data.texts.get(REPORT_TEXT_NAME) or bpy.data.texts.new(REPORT_TEXT_NAME)
    text.clear()
    text.write("\n".join(lines) + "\n")

    def draw(self, _context):
        for line in lines:
            self.layout.label(text=line)

    try:
        bpy.context.window_manager.popup_menu(draw, title=REPORT_TEXT_NAME, icon="ARMATURE_DATA")
    except (AttributeError, RuntimeError):
        pass  # background mode: the Text datablock is still written
    return text


def main():
    rig = bpy.data.objects.get(RIG_NAME)
    if rig is None or rig.type != "ARMATURE":
        raise RuntimeError(
            f"No armature named {RIG_NAME} in this file. Run create_wood_bow.py first."
        )
    if rig.animation_data is None:
        rig.animation_data_create()

    clips = sorted(
        (action for action in bpy.data.actions if action.name.startswith(CLIP_PREFIX)),
        key=lambda action: action.name,
    )
    if not clips:
        raise RuntimeError(
            f"No {CLIP_PREFIX}* Actions in this file, so the bake never finished. Re-run "
            "create_wood_bow.py and read the console: it prints one line per clip."
        )

    lines = [f"{len(clips)} clip(s) in this file, played through {RIG_NAME}:"]
    for action in clips:
        span = key_span(action)
        if span is None:
            lines.append(f"  {action.name:20} EMPTY -- no keys at all")
            continue
        travel = measure(rig, action, span)
        note = "" if travel > 0.5 else "   <- flat: this clip moves nothing"
        lines.append(
            f"  {action.name:20} frames {span[0]:.0f}..{span[1]:.0f}  "
            f"{len(get_fcurves(action))} curves  string travel {travel:6.1f} mm{note}"
        )

    wanted = bpy.data.actions.get(CLIP) or clips[0]
    span = key_span(wanted) or (0, 0)
    rig.animation_data.action = wanted
    slot = bind_action_slot(rig, wanted)
    scene = bpy.context.scene
    scene.frame_start, scene.frame_end = int(round(span[0])), int(round(span[1]))
    scene.frame_set(scene.frame_start)
    lines.append(
        f"Loaded {wanted.name} on {RIG_NAME} (slot {getattr(slot, 'name', None)!r}), frame "
        f"range {scene.frame_start}..{scene.frame_end}. Press Space in the viewport."
    )
    lines.append(
        "To switch clips: edit CLIP at the top of this file and re-run, or use the browse "
        "icon in Dope Sheet > Action Editor."
    )
    report(lines)


if __name__ == "__main__":
    main()
