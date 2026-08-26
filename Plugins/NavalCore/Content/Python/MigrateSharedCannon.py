"""One-time move of the cannon out of the GameFeatures and into NavalCore.

The gun used to exist twice: /OceanAdventure/Naval/BP_Naval_GroundCannon carried the art and
the ballistics, /Raft/Naval/BP_Raft_HeavyCannon was an empty subclass of the same C++ class
that the build system spawned on a deck. Design 7.10 says those are one weapon, so this
collapses them into /NavalCore/Naval/BP_Naval_Cannon and points both call sites at it.

Assets are renamed rather than copied: a .uasset stores its own package path, so moving one on
disk breaks it, and only unreal.EditorAssetLibrary.rename_asset leaves the redirectors that
keep L_NavalP0's placed guns working until the map is re-saved.

The shell and the materials have to travel with the gun. If they stayed behind, NavalCore
content would reference a GameFeature's content -- the dependency the layering rules exist to
prevent, and a real load-order hazard once a feature is disabled.

Run once, in the editor, after setting "CanContainContent": true on NavalCore.uplugin and
restarting. Safe to re-run: anything already moved is skipped.

    import MigrateSharedCannon
    MigrateSharedCannon.main()

Afterwards re-run the two authoring scripts (CreateNavalCoreCannon.py, and the Raft feature's
CreateRaftNavalAssets.py), then open L_NavalP0 and save it so the placed guns stop going
through a redirector.
"""

import unreal


PLUGIN_ROOT = "/NavalCore"
NAVAL_ROOT = f"{PLUGIN_ROOT}/Naval"

CANNON_BLUEPRINT_PATH = f"{NAVAL_ROOT}/BP_Naval_Cannon"
DECK_CANNON_PIECE_PATH = "/Raft/Build/Pieces/DA_BuildPiece_Raft_HeavyCannon"
RAFT_CANNON_BLUEPRINT_PATH = "/Raft/Naval/BP_Raft_HeavyCannon"

# (source, destination). Order does not matter -- renames fix references through redirectors.
MOVES = (
    ("/OceanAdventure/Naval/BP_Naval_GroundCannon", CANNON_BLUEPRINT_PATH),
    (
        "/OceanAdventure/Naval/Projectiles/BP_Naval_CannonballProjectile",
        f"{NAVAL_ROOT}/BP_Naval_CannonballProjectile",
    ),
    (
        "/OceanAdventure/ArtSource/Naval/GroundCannon/SM_Naval_GroundCannon",
        f"{NAVAL_ROOT}/Meshes/SM_Naval_Cannon",
    ),
    ("/OceanAdventure/Naval/Models/SM_Naval_Cannonball", f"{NAVAL_ROOT}/Meshes/SM_Naval_Cannonball"),
)

MATERIAL_NAMES = (
    "M_Cannon_Bore",
    "M_Cannon_Bronze",
    "M_Cannon_DarkMetal",
    "M_Cannon_DarkWood",
    "M_Cannon_WheelRim",
    "M_Cannon_Wood",
)
MATERIAL_SOURCE_ROOT = "/OceanAdventure/ArtSource/Naval/GroundCannon"
MATERIAL_DESTINATION_ROOT = f"{NAVAL_ROOT}/Materials"

# Directories this migration empties. Only removed when nothing else was left in them.
CLEANUP_DIRECTORIES = (
    MATERIAL_SOURCE_ROOT,
    "/OceanAdventure/ArtSource/Naval",
    "/OceanAdventure/ArtSource",
    "/OceanAdventure/Naval/Projectiles",
    "/OceanAdventure/Naval/Models",
)

REDIRECTOR_SCAN_ROOTS = ("/OceanAdventure", "/Raft")


def log(message):
    unreal.log(f"[MigrateSharedCannon] {message}")


def warn(message):
    unreal.log_warning(f"[MigrateSharedCannon] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def move_asset(source, destination):
    if unreal.EditorAssetLibrary.does_asset_exist(destination):
        if unreal.EditorAssetLibrary.does_asset_exist(source):
            warn(
                f"{source} and {destination} both exist. Leaving both alone -- delete the "
                "stale one by hand so this cannot pick the wrong gun."
            )
        else:
            log(f"Already migrated: {destination}")
        return False

    if not unreal.EditorAssetLibrary.does_asset_exist(source):
        warn(f"Nothing to move: {source} does not exist")
        return False

    require(
        unreal.EditorAssetLibrary.rename_asset(source, destination),
        f"Unable to move {source} -> {destination}",
    )
    log(f"Moved {source} -> {destination}")
    return True


def repoint_deck_cannon_piece():
    """The raft's build piece stops spawning its own copy and spawns the shared gun.

    The authoring script does this too, on every run. It is repeated here so the piece is
    never left pointing at a Blueprint this script is about to delete.
    """
    piece = unreal.EditorAssetLibrary.load_asset(DECK_CANNON_PIECE_PATH)
    if piece is None:
        warn(f"{DECK_CANNON_PIECE_PATH} is missing; skipping the deck cannon repoint")
        return

    require(
        unreal.EditorAssetLibrary.does_asset_exist(CANNON_BLUEPRINT_PATH),
        f"{CANNON_BLUEPRINT_PATH} does not exist yet. The move above should have created it; "
        "run NavalCore's CreateNavalCoreCannon.py if the old gun was already gone.",
    )
    cannon_class = require(
        unreal.EditorAssetLibrary.load_blueprint_class(CANNON_BLUEPRINT_PATH),
        f"Unable to resolve the generated class of {CANNON_BLUEPRINT_PATH}",
    )
    spawn_fragment_class = require(
        getattr(unreal, "BuildPieceFragment_SpawnActor", None),
        "BuildPieceFragment_SpawnActor is unavailable; compile BuildingCoreRuntime first",
    )

    repointed = False
    for fragment in piece.get_editor_property("fragments"):
        if fragment is not None and isinstance(fragment, spawn_fragment_class):
            fragment.set_editor_property("actor_class", cannon_class)
            repointed = True

    require(repointed, f"{DECK_CANNON_PIECE_PATH} has no SpawnActor fragment to repoint")
    unreal.EditorAssetLibrary.save_loaded_asset(piece, only_if_is_dirty=False)
    log(f"{DECK_CANNON_PIECE_PATH} now spawns {CANNON_BLUEPRINT_PATH}")


def delete_raft_cannon_blueprint():
    if not unreal.EditorAssetLibrary.does_asset_exist(RAFT_CANNON_BLUEPRINT_PATH):
        return
    if unreal.EditorAssetLibrary.delete_asset(RAFT_CANNON_BLUEPRINT_PATH):
        log(f"Deleted the duplicate {RAFT_CANNON_BLUEPRINT_PATH}")
    else:
        warn(
            f"Could not delete {RAFT_CANNON_BLUEPRINT_PATH}. Check its referencers in the "
            "Content Browser -- something other than the deck piece still points at it."
        )


def fixup_redirectors():
    """Collapse the redirectors the renames left behind, so nothing keeps the old paths alive."""
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    redirectors = []
    for root in REDIRECTOR_SCAN_ROOTS:
        for asset_data in registry.get_assets_by_path(root, recursive=True):
            # asset_class_path is the UE5 spelling; asset_class is the older one.
            class_path = getattr(asset_data, "asset_class_path", None)
            class_name = str(class_path.asset_name) if class_path else str(
                getattr(asset_data, "asset_class", "")
            )
            if class_name != "ObjectRedirector":
                continue
            redirector = asset_data.get_asset()
            if redirector is not None:
                redirectors.append(redirector)

    if not redirectors:
        log("No redirectors left to fix up")
        return

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    fixup = getattr(asset_tools, "fixup_referencers", None)
    if fixup is None:
        warn(
            f"{len(redirectors)} redirectors remain and this engine build does not expose "
            "fixup_referencers to Python. Right-click /OceanAdventure and /Raft in the "
            "Content Browser and run Fix Up Redirectors In Folder."
        )
        return

    fixup(redirectors)
    log(f"Fixed up {len(redirectors)} redirectors")


def cleanup_empty_directories():
    for directory in CLEANUP_DIRECTORIES:
        if not unreal.EditorAssetLibrary.does_directory_exist(directory):
            continue
        if unreal.EditorAssetLibrary.list_assets(directory, recursive=True, include_folder=False):
            continue
        if unreal.EditorAssetLibrary.delete_directory(directory):
            log(f"Removed the now-empty {directory}")


def main():
    require(
        unreal.EditorAssetLibrary.does_directory_exist(PLUGIN_ROOT),
        f"{PLUGIN_ROOT} is not mounted. Set \"CanContainContent\": true in NavalCore.uplugin "
        "and restart the editor.",
    )
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([PLUGIN_ROOT, "/OceanAdventure", "/Raft"], True, True)

    for source, destination in MOVES:
        move_asset(source, destination)
    for material_name in MATERIAL_NAMES:
        move_asset(
            f"{MATERIAL_SOURCE_ROOT}/{material_name}",
            f"{MATERIAL_DESTINATION_ROOT}/{material_name}",
        )

    repoint_deck_cannon_piece()
    delete_raft_cannon_blueprint()
    fixup_redirectors()
    cleanup_empty_directories()

    log(
        "Migration done. Re-run CreateNavalCoreCannon.py and the Raft feature's "
        "CreateRaftNavalAssets.py, then open L_NavalP0 and save it."
    )


if __name__ == "__main__":
    main()
