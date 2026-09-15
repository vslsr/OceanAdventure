"""Import the procedurally generated wood bow and give it the contract content relies on.

    /OceanAdventure/Weapons/Bow/SK_WoodBow      the skinned bow
    <its skeleton>                              reused across runs, never re-created
    socket 'nock' on bone string_mid            where the arrow attaches

The source is authored by ``blender/script/python/create_wood_bow.py`` and exported to
``blender/models/SK_WoodBow.fbx``. Run that first.

The bow is the player's weapon, so it belongs to the gameplay-layer GameFeature that owns
the player Pawn -- this one. Nothing here may reference /Raft or another feature's content.

Run in the full Unreal Editor (Output Log, Cmd mode)::

    py "C:/EpicWkspc/OceanAdventure/Plugins/GameFeatures/OceanAdventure/Content/Python/CreateWoodBowAssets.py"

Safe to re-run. In the full Editor a re-run re-imports the FBX, so a Blender revision lands;
in a PythonScript commandlet it reuses what exists instead (see import_or_reuse_bow below).

What this script deliberately does NOT do: author ABP_WoodBow's AnimGraph. Anim graph nodes
are not exposed to Python, so the draw itself -- rotate limb_upper/limb_lower, translate
string_mid from a 0..1 charge -- is a hand-authored AnimBlueprint. This script's job is to
guarantee the bones and the socket that AnimBlueprint is written against actually exist.
"""

import ast
from pathlib import Path

import unreal


FEATURE_ROOT = "/OceanAdventure"
BOW_ROOT = f"{FEATURE_ROOT}/Weapons/Bow"
BOW_MESH_NAME = "SK_WoodBow"
BOW_MESH_PATH = f"{BOW_ROOT}/{BOW_MESH_NAME}"

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


def read_bone_contract():
    """Read EXPECTED_BONES straight out of the Blender script that generates the rig.

    The bone names are a contract spanning two interpreters, two languages and a binary
    file in between. Written down twice they drift, and the drift is silent: an AnimBlueprint
    that drives a bone which no longer exists does not error, it just stops animating. So
    there is one literal, in the script that creates the bones, and this side reads it.

    Returns ``None`` (with a warning) in a content-only checkout that has no blender/ tree;
    the import still runs, only the cross-check is unavailable.
    """
    if not BOW_BLENDER_SCRIPT.is_file():
        warn(
            f"No {BOW_BLENDER_SCRIPT.name} in this checkout, so the bone contract cannot be "
            "cross-checked. The imported skeleton is taken on trust."
        )
        return None

    tree = ast.parse(BOW_BLENDER_SCRIPT.read_text(encoding="utf-8"))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        targets = [t.id for t in node.targets if isinstance(t, ast.Name)]
        if "EXPECTED_BONES" not in targets:
            continue
        # literal_eval, never eval: this file is parsed, not executed.
        bones = tuple(ast.literal_eval(node.value))
        require(bones, f"EXPECTED_BONES in {BOW_BLENDER_SCRIPT.name} is empty")
        return bones

    raise RuntimeError(
        f"{BOW_BLENDER_SCRIPT.name} no longer declares EXPECTED_BONES as a plain literal. "
        "Keep it a literal tuple; this script reads it with ast.literal_eval."
    )


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


def validate_bone_contract(mesh, expected_bones):
    if expected_bones is None:
        return
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

    expected_bones = read_bone_contract()
    mesh, skeleton = import_or_reuse_bow()
    require(skeleton, f"{BOW_MESH_PATH} has no Skeleton to author sockets on")
    validate_bone_contract(mesh, expected_bones)
    ensure_sockets(skeleton)
    save(mesh)

    log(f"WOOD_BOW_ASSETS_OK {BOW_MESH_PATH} on {package_of(skeleton)}")
    log(
        "Remaining by hand: author ABP_WoodBow against this skeleton. Drive limb_upper and "
        "limb_lower with a mirrored X rotation and translate string_mid along +X (Unreal) "
        "from the 0..1 draw charge; anim graph nodes are not exposed to Python."
    )


if __name__ == "__main__":
    main()
