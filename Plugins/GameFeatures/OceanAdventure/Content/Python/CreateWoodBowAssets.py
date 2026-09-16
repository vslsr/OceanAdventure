"""Import the procedurally generated wood bow and give it the contract content relies on.

    /OceanAdventure/Weapons/Bow/SK_WoodBow          the skinned bow
    <its skeleton>                                  reused across runs, never re-created
    socket 'nock' on bone string_mid                where the arrow attaches
    four AnimSequences under that folder             idle / draw / aim / release

The source is authored by ``blender/script/python/create_wood_bow.py`` and exported as two
files: ``blender/models/SK_WoodBow.fbx`` (mesh + skeleton) and
``blender/models/A_WoodBow_Clips.fbx`` (every clip, one animation stack each). Run it first.

The clips arrive in one file because this is a single prop whose four clips drive the same
three bones and are revised together. The cost is that *Unreal* names the assets: importing
a multi-stack FBX, the importer derives one AnimSequence name per stack, and that naming is
the engine's to decide, not this script's. So nothing here assumes an asset path. The
import is run once, whatever AnimSequences it produced are discovered through the Asset
Registry, and each clip is claimed by matching the Blender Action name inside the asset
name. A clip that cannot be claimed is a stop, not a warning -- the alternative is an
AnimBlueprint silently wired to a clip that is not the one it names.

The bow is the player's weapon, so it belongs to the gameplay-layer GameFeature that owns
the player Pawn -- this one. Nothing here may reference /Raft or another feature's content.

Run in the full Unreal Editor (Output Log, Cmd mode)::

    py "C:/EpicWkspc/OceanAdventure/Plugins/GameFeatures/OceanAdventure/Content/Python/CreateWoodBowAssets.py"

Safe to re-run. In the full Editor a re-run re-imports the FBX, so a Blender revision lands;
in a PythonScript commandlet it reuses what exists instead (see import_or_reuse_bow below).

The four clips are driven three different ways and the AnimBlueprint must treat them that
way. A_WoodBow_Draw is a pose ramp, not a performance: sample it at an explicit time of
``charge * length`` rather than playing it. A_WoodBow_Release is the opposite -- play it
once, on the clock, when the arrow leaves. A_WoodBow_Idle and A_WoodBow_Aim are loops:
play them looping while the bow is carried and while the draw is held respectively.

The seams are already authored, in Blender: idle rests on the draw's first pose, aim holds
the draw's last, and the release starts from that same held pose. State changes therefore
need no crossfade to hide a step, and a crossfade long enough to hide one would only smear
the snap out of the loose.

What this script deliberately does NOT do: author ABP_WoodBow's AnimGraph. Anim graph nodes
are not exposed to Python, so wiring those two clips together is hand work. This script's
job is to guarantee the bones, the socket and the clips that AnimBlueprint is written
against actually exist, and that the clips still carry the motion Blender baked.
"""

import ast
from pathlib import Path

import unreal


FEATURE_ROOT = "/OceanAdventure"
BOW_ROOT = f"{FEATURE_ROOT}/Weapons/Bow"
BOW_MESH_NAME = "SK_WoodBow"
BOW_MESH_PATH = f"{BOW_ROOT}/{BOW_MESH_NAME}"

#: Blender Action names, in the order content thinks about them. Each must be claimable
#: from exactly one imported AnimSequence. Lengths come from the Blender script's
#: CLIP_SECONDS literal, so they are stated in one place only.
BOW_CLIP_ACTIONS = ("WoodBow_Idle", "WoodBow_Draw", "WoodBow_Aim", "WoodBow_Release")

#: Clips the AnimBlueprint plays looping. Unreal decides looping at the play node, not on
#: the asset, so this script cannot set it -- it reports it, and the graph honours it.
BOW_LOOPING_CLIPS = ("WoodBow_Idle", "WoodBow_Aim")

#: The draw is sampled by charge rather than played, so its length is a 0..1 ruler and
#: proves nothing about timing. Every other clip's length is a design value worth checking.
BOW_UNTIMED_CLIPS = ("WoodBow_Draw",)

PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
BLENDER_MODELS = PROJECT_ROOT / "blender" / "models"
BOW_SOURCE_FBX = BLENDER_MODELS / f"{BOW_MESH_NAME}.fbx"
BOW_BLENDER_SCRIPT = PROJECT_ROOT / "blender" / "script" / "python" / "create_wood_bow.py"

#: Sockets this script owns. The arrow attaches to a socket rather than a bone because
#: string_mid is a deform bone the draw animation rewrites every frame, and because a leaf
#: non-deform bone would not survive the FBX export's use_armature_deform_only at all.
OWNED_SOCKETS = (
    ("nock", "string_mid"),
)


def log(message):
    unreal.log(f"[WoodBow] {message}")


def warn(message):
    unreal.log_warning(f"[WoodBow] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def split_path(asset_path):
    package_path, _, asset_name = asset_path.rpartition("/")
    return package_path, asset_name


def package_of(asset):
    if asset is None:
        return ""
    return str(asset.get_path_name()).split(".", 1)[0]


def save(asset):
    require(
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False),
        f"Unable to save {asset.get_path_name()}",
    )


def call_first_available(target, method_names, *args):
    """Call whichever of *method_names* this engine build actually exposes.

    Used only where the Python exposure genuinely moved between UE versions (UE5 renamed
    SetSkeletalMesh to SetSkeletalMeshAsset). It never degrades silently: if none of the
    candidates exist the caller gets a RuntimeError naming every name that was tried, so a
    changed exposure shows up as a stop rather than as a check that quietly did nothing.
    """
    for name in method_names:
        method = getattr(target, name, None)
        if callable(method):
            return method(*args)
    raise RuntimeError(
        f"{type(target).__name__} exposes none of {list(method_names)} in this engine build. "
        "Check the current UE Python exposure and update this call."
    )


def is_commandlet_host():
    return "-run=pythonscript" in str(unreal.SystemLibrary.get_command_line()).lower()


# --- The bone contract ------------------------------------------------------


def read_blender_contract():
    """Read the rig contract straight out of the Blender script that generates it.

    Bone names, the baked sample rate and the length of the loose are a contract spanning
    two interpreters, two languages and a binary file in between. Written down twice they
    drift, and the drift is silent: an AnimBlueprint driving a bone that no longer exists
    does not error, it just stops animating, and a clip resampled at the wrong rate merely
    looks a bit dull. So there is one set of literals, in the script that produces them,
    and this side reads them.

    Returns ``None`` (with a warning) in a content-only checkout with no blender/ tree;
    the import still runs, only the cross-checks are unavailable.
    """
    if not BOW_BLENDER_SCRIPT.is_file():
        warn(
            f"No {BOW_BLENDER_SCRIPT.name} in this checkout, so the rig contract cannot be "
            "cross-checked. The imported skeleton and clips are taken on trust."
        )
        return None

    wanted = (
        "EXPECTED_BONES",
        "ANIMATION_FPS",
        "CLIPS_FBX_NAME",
        "CLIP_SECONDS",
    )
    found = {}
    tree = ast.parse(BOW_BLENDER_SCRIPT.read_text(encoding="utf-8"))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        for target in node.targets:
            if isinstance(target, ast.Name) and target.id in wanted:
                # literal_eval, never eval: this file is parsed, not executed.
                found[target.id] = ast.literal_eval(node.value)

    missing = [name for name in wanted if name not in found]
    require(
        not missing,
        f"{BOW_BLENDER_SCRIPT.name} no longer declares {missing} as plain literals. "
        "Keep them literal; this script reads them with ast.literal_eval.",
    )
    require(found["EXPECTED_BONES"], f"EXPECTED_BONES in {BOW_BLENDER_SCRIPT.name} is empty")
    found["EXPECTED_BONES"] = tuple(found["EXPECTED_BONES"])
    return found


def read_skeleton_bone_names(mesh):
    """List the imported skeleton's bones through the skinned-component accessors.

    USkeleton's own bone_tree carries only deprecated name fields, so the reference skeleton
    is read the way Blueprint reads it: a transient SkeletalMeshComponent pointed at the mesh.
    It is never registered into a world and never saved.
    """
    component = require(
        unreal.new_object(unreal.SkeletalMeshComponent),
        "Unable to create a transient SkeletalMeshComponent to read the reference skeleton",
    )
    call_first_available(
        component, ("set_skeletal_mesh_asset", "set_skeletal_mesh", "set_skinned_asset"), mesh
    )
    count = int(call_first_available(component, ("get_num_bones",)))
    require(count > 0, f"{BOW_MESH_PATH} imported with no bones; the FBX has no armature")
    return [str(component.get_bone_name(index)) for index in range(count)]


def validate_bone_contract(mesh, contract):
    if contract is None:
        return
    expected_bones = contract["EXPECTED_BONES"]
    actual = read_skeleton_bone_names(mesh)
    missing = [bone for bone in expected_bones if bone not in actual]
    extra = [bone for bone in actual if bone not in expected_bones]
    require(
        not missing and not extra,
        f"{BOW_MESH_PATH} skeleton does not match the rig contract in "
        f"{BOW_BLENDER_SCRIPT.name}: missing={missing} unexpected={extra}. Re-run the Blender "
        "script and re-export the FBX before using this mesh.",
    )
    require(
        actual[0] == expected_bones[0],
        f"{BOW_MESH_PATH} root bone is '{actual[0]}', expected '{expected_bones[0]}'. Unreal "
        "treats bone 0 as the motion root; a different one breaks root motion and retargeting.",
    )
    log(f"Skeleton matches the rig contract: {', '.join(actual)}")


# --- Import -----------------------------------------------------------------


def import_or_reuse_bow():
    """Import SK_WoodBow, reusing the existing Skeleton asset rather than making a new one.

    Two things this has to get right, and both fail quietly if it does not:

    Skeleton reuse. The FBX importer creates a brand new Skeleton whenever it is not handed
    one, so an unguarded re-run leaves SK_WoodBow_Skeleton_1 behind and every AnimBlueprint,
    montage and socket still points at the original. The skeleton is therefore read off the
    mesh that already exists -- not guessed from a name -- and fed back into the import, and
    the mesh's skeleton is asserted unchanged afterwards.

    Host capability (ledger PY-UE-006). UE 5.7's Interchange completion path touches Slate
    even for an automated task, which has no application in a PythonScript commandlet. So the
    full Editor re-imports on every run, letting a Blender revision land; a commandlet reuses
    what is on disk, and refuses outright if the asset does not exist yet.
    """
    existing = (
        unreal.EditorAssetLibrary.load_asset(BOW_MESH_PATH)
        if unreal.EditorAssetLibrary.does_asset_exist(BOW_MESH_PATH)
        else None
    )
    if existing is not None:
        require(
            existing.get_class().get_name() == "SkeletalMesh",
            f"{BOW_MESH_PATH} exists but is a {existing.get_class().get_name()}, not a "
            "SkeletalMesh. Delete it and re-run.",
        )
    previous_skeleton = existing.get_editor_property("skeleton") if existing else None

    if is_commandlet_host():
        require(
            existing is not None,
            f"{BOW_MESH_PATH} needs its first FBX import. Run this script in the full Unreal "
            "Editor once; UE 5.7 Interchange crashes in PythonScript commandlets without Slate.",
        )
        log(f"Commandlet host: reused {BOW_MESH_PATH} without re-importing")
        return existing, previous_skeleton

    require(
        BOW_SOURCE_FBX.is_file(),
        f"Missing {BOW_SOURCE_FBX}. Run blender/script/python/create_wood_bow.py in Blender "
        "first (blender --background --python <that script>).",
    )

    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", True)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("create_physics_asset", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    # The whole point of this line. None on a first import; the existing asset thereafter.
    options.set_editor_property("skeleton", previous_skeleton)
    skeletal_options = options.get_editor_property("skeletal_mesh_import_data")
    skeletal_options.set_editor_property("import_morph_targets", False)

    destination_path, destination_name = split_path(BOW_MESH_PATH)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(BOW_SOURCE_FBX))
    task.set_editor_property("destination_path", destination_path)
    task.set_editor_property("destination_name", destination_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    mesh = require(
        unreal.EditorAssetLibrary.load_asset(BOW_MESH_PATH),
        f"Unable to import {BOW_MESH_PATH} from {BOW_SOURCE_FBX}",
    )
    skeleton = require(
        mesh.get_editor_property("skeleton"),
        f"{BOW_MESH_PATH} imported without a Skeleton",
    )
    if previous_skeleton is not None:
        require(
            package_of(skeleton) == package_of(previous_skeleton),
            f"Re-import moved {BOW_MESH_PATH} onto a new Skeleton "
            f"({package_of(skeleton)} instead of {package_of(previous_skeleton)}). Every "
            "AnimBlueprint and socket still points at the old one; restore it before saving.",
        )
    log(f"Imported {BOW_SOURCE_FBX.name} into {BOW_MESH_PATH} on {package_of(skeleton)}")
    return mesh, skeleton


def clip_source_fbx(contract):
    """Where the bundle lives. Its name is the Blender script's to choose, not ours."""
    name = contract["CLIPS_FBX_NAME"] if contract else "A_WoodBow_Clips.fbx"
    return BLENDER_MODELS / name


def anim_sequences_under(path, skeleton):
    """Every AnimSequence under *path* that is bound to *skeleton*, by asset path.

    Bound to *this* skeleton is the part that matters: an AnimSequence on another skeleton
    loads fine and simply never plays, so it must not be claimable as one of our clips.
    """
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([path], True, True)
    found = {}
    for asset_data in registry.get_assets_by_path(path, recursive=True):
        asset = unreal.EditorAssetLibrary.load_asset(str(asset_data.package_name))
        if asset is None or asset.get_class().get_name() != "AnimSequence":
            continue
        bound = asset.get_editor_property("skeleton")
        if bound is not None and package_of(bound) == package_of(skeleton):
            found[package_of(asset)] = asset
    return found


def claim_clips(imported, contract):
    """Match each Blender Action to exactly one imported AnimSequence, by name.

    Unreal derives the asset name from the FBX animation stack, which Blender in turn named
    after the Action -- so the Action name survives inside the asset name even though its
    exact shape (prefix, separator, casing) is the importer's business. Matching on the
    part both ends agree on is what keeps this script from hard-coding a naming convention
    it does not own.
    """
    def squash(text):
        return "".join(character for character in text if character.isalnum()).lower()

    claimed = {}
    for action_name in BOW_CLIP_ACTIONS:
        # "WoodBow_Idle" -> "idle": the stack name may carry the rig or file name too, so
        # match on the part that distinguishes the clips from each other.
        token = squash(action_name.split("_")[-1])
        matches = [
            (asset_path, asset)
            for asset_path, asset in imported.items()
            if token in squash(asset_path.rsplit("/", 1)[-1])
        ]
        require(
            len(matches) == 1,
            f"Cannot tell which imported AnimSequence is {action_name}: "
            f"{[path for path, _ in matches] or 'nothing matched'} out of "
            f"{sorted(imported)}. The importer named the stacks differently than expected; "
            "fix the mapping here rather than guessing in the AnimBlueprint.",
        )
        claimed[action_name] = matches[0]

    unclaimed = sorted(set(imported) - {path for path, _ in claimed.values()})
    if unclaimed:
        warn(
            f"{len(unclaimed)} AnimSequence(s) on this skeleton were not claimed by any "
            f"clip: {unclaimed}. Stale assets from an older import layout?"
        )
    return claimed


def import_or_reuse_clips(skeleton, contract):
    """Import the one bundle FBX and work out which AnimSequence is which clip.

    The Skeleton is passed in, never guessed: an AnimSequence imported without one is
    either rejected or lands on a freshly invented skeleton, and either way the
    AnimBlueprint cannot play it.

    The sample rate is asked for explicitly. Unreal resamples imported animation to a
    default 30Hz, and the release crosses rest twice inside 0.12s -- about 17Hz -- so 30Hz
    lands under two samples per cycle and the wobble aliases into a shrug.
    """
    source_fbx = clip_source_fbx(contract)
    existing = anim_sequences_under(BOW_ROOT, skeleton)

    if is_commandlet_host():
        require(
            len(existing) >= len(BOW_CLIP_ACTIONS),
            f"{BOW_ROOT} holds {len(existing)} AnimSequence(s) on {package_of(skeleton)}, "
            f"fewer than the {len(BOW_CLIP_ACTIONS)} clips. They need their first FBX import "
            "in the full Unreal Editor; UE 5.7 Interchange crashes in PythonScript "
            "commandlets without Slate.",
        )
        log(f"Commandlet host: reused {len(existing)} clip(s) without re-importing")
        return claim_clips(existing, contract)

    require(
        source_fbx.is_file(),
        f"Missing {source_fbx}. Run blender/script/python/create_wood_bow.py in Blender "
        "first; it exports the mesh and the clip bundle together.",
    )

    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", False)
    options.set_editor_property("import_as_skeletal", True)
    options.set_editor_property("import_animations", True)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("create_physics_asset", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    options.set_editor_property("skeleton", skeleton)
    anim_options = options.get_editor_property("anim_sequence_import_data")
    anim_options.set_editor_property("import_bone_tracks", True)
    # Keep every baked key. Both of these throw motion away, and the motion they throw
    # away first is exactly the short, small, fast kind the release is made of.
    anim_options.set_editor_property("remove_redundant_keys", False)
    if contract is not None:
        anim_options.set_editor_property("use_default_sample_rate", False)
        anim_options.set_editor_property("custom_sample_rate", int(contract["ANIMATION_FPS"]))

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source_fbx))
    task.set_editor_property("destination_path", BOW_ROOT)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    imported = anim_sequences_under(BOW_ROOT, skeleton)
    require(
        len(imported) >= len(BOW_CLIP_ACTIONS),
        f"{source_fbx.name} produced {len(imported)} AnimSequence(s), expected at least "
        f"{len(BOW_CLIP_ACTIONS)}: {sorted(imported)}. This engine build took only part of "
        "the bundle. Set BUNDLE_CLIPS_IN_ONE_FBX = False in "
        f"{BOW_BLENDER_SCRIPT.name}, re-run it, and import one FBX per clip instead.",
    )
    log(f"Imported {source_fbx.name}: {len(imported)} AnimSequence(s) under {BOW_ROOT}")
    return claim_clips(imported, contract)


def validate_clips(claimed, skeleton, contract):
    for action_name, (asset_path, clip) in claimed.items():
        length = float(call_first_available(clip, ("get_play_length",)))
        require(length > 0.0, f"{asset_path} imported with zero length")

        if contract is not None and action_name not in BOW_UNTIMED_CLIPS:
            # A clip's length is the one design value that can prove the trip through FBX
            # kept the timing. A truncated loop is worse off than a truncated one-shot: the
            # cycle still plays, just slightly wrong, for as long as the bow is on screen.
            expected = float(contract["CLIP_SECONDS"][action_name])
            frame_slack = 2.0 / float(contract["ANIMATION_FPS"])
            require(
                abs(length - expected) <= expected * 0.25 + frame_slack,
                f"{asset_path} ({action_name}) is {length:.4f}s, expected about "
                f"{expected:.4f}s. The clip was resampled or truncated on import.",
            )
        role = " (play it looping)" if action_name in BOW_LOOPING_CLIPS else ""
        log(f"{action_name} -> {asset_path}, {length:.4f}s on {package_of(skeleton)}{role}")


# --- Sockets ----------------------------------------------------------------


def ensure_sockets(skeleton):
    """Rebuild the sockets this script owns, preserving any a human added by hand.

    Filtering is by socket name -- a plain string compare, which is the one comparison the
    UE Python wrappers do faithfully (ledger PY-LYRA-003/004: `in` over UObject or struct
    wrappers falls back to identity, so the filter never matches and the rebuild silently
    degrades into an append-only accumulator).
    """
    owned_names = {name for name, _ in OWNED_SOCKETS}
    current = list(skeleton.get_editor_property("sockets") or [])
    preserved = [
        socket
        for socket in current
        if str(socket.get_editor_property("socket_name")) not in owned_names
    ]
    reused = {
        str(socket.get_editor_property("socket_name")): socket
        for socket in current
        if str(socket.get_editor_property("socket_name")) in owned_names
    }

    rebuilt = list(preserved)
    for socket_name, bone_name in OWNED_SOCKETS:
        # Reuse the existing object when there is one: creating a second UObject under the
        # same name in the same outer collides, and re-running must not churn the asset.
        socket = reused.get(socket_name) or require(
            unreal.new_object(
                unreal.SkeletalMeshSocket, outer=skeleton, name=unreal.Name(socket_name)
            ),
            f"Unable to create socket {socket_name} on {package_of(skeleton)}",
        )
        socket.set_editor_property("socket_name", unreal.Name(socket_name))
        socket.set_editor_property("bone_name", unreal.Name(bone_name))
        socket.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, 0.0))
        socket.set_editor_property("relative_rotation", unreal.Rotator(0.0, 0.0, 0.0))
        socket.set_editor_property("relative_scale", unreal.Vector(1.0, 1.0, 1.0))
        rebuilt.append(socket)

    skeleton.set_editor_property("sockets", rebuilt)
    save(skeleton)

    # Read back from the saved asset. An existence check is not enough: it returns True for a
    # duplicated entry, which is exactly the failure this rebuild exists to prevent.
    final = list(skeleton.get_editor_property("sockets") or [])
    require(
        len(final) == len(preserved) + len(OWNED_SOCKETS),
        f"{package_of(skeleton)} has {len(final)} sockets, expected "
        f"{len(preserved) + len(OWNED_SOCKETS)}; the owned-socket rebuild is accumulating.",
    )
    for socket_name, bone_name in OWNED_SOCKETS:
        matches = [
            socket
            for socket in final
            if str(socket.get_editor_property("socket_name")) == socket_name
        ]
        require(len(matches) == 1, f"Socket {socket_name} appears {len(matches)} times")
        retained = str(matches[0].get_editor_property("bone_name"))
        require(
            retained == bone_name,
            f"Socket {socket_name} is on bone '{retained}', expected '{bone_name}'",
        )
        log(f"Socket '{socket_name}' is on bone '{bone_name}'")


def main():
    require(
        unreal.EditorAssetLibrary.does_directory_exist(FEATURE_ROOT),
        f"{FEATURE_ROOT} is not mounted. Enable the OceanAdventure GameFeature and restart "
        "the editor.",
    )
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([BOW_ROOT], True, True)

    contract = read_blender_contract()
    mesh, skeleton = import_or_reuse_bow()
    require(skeleton, f"{BOW_MESH_PATH} has no Skeleton to author sockets on")
    validate_bone_contract(mesh, contract)
    ensure_sockets(skeleton)
    save(mesh)

    claimed = import_or_reuse_clips(skeleton, contract)
    validate_clips(claimed, skeleton, contract)

    log(f"WOOD_BOW_ASSETS_OK {BOW_MESH_PATH} on {package_of(skeleton)}")
    log(
        "Remaining by hand: author ABP_WoodBow against this skeleton, using the clip paths "
        "logged above. Loop the idle clip while the bow is carried; sample the draw clip at "
        "an explicit time of charge * length while it is being pulled -- do not play it; "
        "loop the aim clip while the draw is held; play the release clip once when the arrow "
        "leaves. The clips already share their seam poses, so these transitions need no "
        "blend time. Anim graph nodes are not exposed to Python."
    )


if __name__ == "__main__":
    main()
