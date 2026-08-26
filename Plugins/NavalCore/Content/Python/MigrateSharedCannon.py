"""One-time move of the cannon out of the GameFeatures and into NavalCore.

The gun used to exist twice: /OceanAdventure/Naval/BP_Naval_GroundCannon carried the art and
the ballistics, /Raft/Naval/BP_Raft_HeavyCannon was an empty subclass of the same C++ class
that the build system spawned on a deck. Design 7.10 says those are one weapon, so this
collapses them into /NavalCore/Naval/BP_Naval_Cannon and points both call sites at it.

Assets are renamed rather than copied: a .uasset stores its own package path, so moving one on
disk breaks it, and only unreal.EditorAssetLibrary.rename_asset leaves the redirectors that
keep L_NavalP0's placed guns working until the map is re-saved.

Every material and mesh the gun and its shell reach has to travel with them. If one stayed
behind, NavalCore content would reference a GameFeature's content -- the dependency the
layering rules exist to prevent, which Lyra's AssetValidator_AssetReferenceRestrictions fails
the build over, and a real load-order hazard once a feature is disabled.

Run in the editor, after setting "CanContainContent": true on NavalCore.uplugin and restarting.
Safe to re-run, and re-running is also the repair path: a package saved before its own
dependencies moved still names their old locations on disk, so the final step force-saves every
destination once everything has landed. Re-run it BEFORE restarting the editor if a previous run
reported missing dependencies -- the fix relies on the objects still being resolved in memory.

    import MigrateSharedCannon
    MigrateSharedCannon.main()

Re-running inside the same editor session needs a reload -- import hands back the module object
it cached the first time, so an edited file is silently ignored and the old code runs again:

    import importlib, MigrateSharedCannon
    importlib.reload(MigrateSharedCannon)
    MigrateSharedCannon.main()

Afterwards re-run the two authoring scripts (CreateNavalCoreCannon.py, and the Raft feature's
CreateRaftNavalAssets.py), then open L_NavalP0 and save it so the placed guns stop going
through a redirector.
"""

import unreal


PLUGIN_ROOT = "/NavalCore"
NAVAL_ROOT = f"{PLUGIN_ROOT}/Naval"
# Art is grouped per weapon under NavalArts. These are where the migration puts things; an
# asset already sitting somewhere else under /NavalCore counts as migrated wherever it is.
ART_ROOT = f"{PLUGIN_ROOT}/NavalArts"
MESH_ROOT = f"{ART_ROOT}/Cannon/Meshes"
MATERIAL_ROOT = f"{ART_ROOT}/Cannon/Materials"

CANNON_BLUEPRINT_PATH = f"{NAVAL_ROOT}/BP_Naval_Cannon"
PROJECTILE_BLUEPRINT_PATH = f"{NAVAL_ROOT}/BP_Naval_CannonballProjectile"
CANNON_MESH_PATH = f"{MESH_ROOT}/SM_Naval_Cannon"
CANNONBALL_MESH_PATH = f"{MESH_ROOT}/SM_Naval_Cannonball"

DECK_CANNON_PIECE_PATH = "/Raft/Build/Pieces/DA_BuildPiece_Raft_HeavyCannon"
RAFT_CANNON_BLUEPRINT_PATH = "/Raft/Naval/BP_Raft_HeavyCannon"

GROUND_CANNON_ART_ROOT = "/OceanAdventure/ArtSource/Naval/GroundCannon"
CANNONBALL_ART_ROOT = "/OceanAdventure/Naval/Models"

# (source, destination). Order does not matter: renames leave redirectors, and the final
# re-save is what writes the new paths into every package that survived the move.
MOVES = (
    ("/OceanAdventure/Naval/BP_Naval_GroundCannon", CANNON_BLUEPRINT_PATH),
    ("/OceanAdventure/Naval/Projectiles/BP_Naval_CannonballProjectile", PROJECTILE_BLUEPRINT_PATH),
    (f"{GROUND_CANNON_ART_ROOT}/SM_Naval_GroundCannon", CANNON_MESH_PATH),
    (f"{CANNONBALL_ART_ROOT}/SM_Naval_Cannonball", CANNONBALL_MESH_PATH),
    # The gun's materials.
    (f"{GROUND_CANNON_ART_ROOT}/M_Cannon_Bore", f"{MATERIAL_ROOT}/M_Cannon_Bore"),
    (f"{GROUND_CANNON_ART_ROOT}/M_Cannon_Bronze", f"{MATERIAL_ROOT}/M_Cannon_Bronze"),
    (f"{GROUND_CANNON_ART_ROOT}/M_Cannon_DarkMetal", f"{MATERIAL_ROOT}/M_Cannon_DarkMetal"),
    (f"{GROUND_CANNON_ART_ROOT}/M_Cannon_DarkWood", f"{MATERIAL_ROOT}/M_Cannon_DarkWood"),
    (f"{GROUND_CANNON_ART_ROOT}/M_Cannon_WheelRim", f"{MATERIAL_ROOT}/M_Cannon_WheelRim"),
    (f"{GROUND_CANNON_ART_ROOT}/M_Cannon_Wood", f"{MATERIAL_ROOT}/M_Cannon_Wood"),
    # The shell's materials. They sit next to the cannonball mesh rather than with the gun's,
    # which is why they are easy to miss -- and the reference validator does not miss them.
    (f"{CANNONBALL_ART_ROOT}/M_Cannonball_DarkIron", f"{MATERIAL_ROOT}/M_Cannonball_DarkIron"),
    (f"{CANNONBALL_ART_ROOT}/M_Cannonball_Fuse", f"{MATERIAL_ROOT}/M_Cannonball_Fuse"),
    (f"{CANNONBALL_ART_ROOT}/M_Cannonball_FusePlug", f"{MATERIAL_ROOT}/M_Cannonball_FusePlug"),
    (f"{CANNONBALL_ART_ROOT}/M_Cannonball_Iron", f"{MATERIAL_ROOT}/M_Cannonball_Iron"),
)

# Emptied by the migration. Only removed once every redirector in them has been resolved --
# deleting a redirector that something still points at is what strands a reference.
CLEANUP_DIRECTORIES = (
    GROUND_CANNON_ART_ROOT,
    "/OceanAdventure/ArtSource/Naval",
    "/OceanAdventure/ArtSource",
    "/OceanAdventure/Naval/Projectiles",
    CANNONBALL_ART_ROOT,
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


def find_asset_by_name(asset_name):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for asset_data in registry.get_assets_by_path(PLUGIN_ROOT, recursive=True):
        if str(asset_data.asset_name) == asset_name:
            return str(asset_data.package_name)
    return None


def move_asset(source, destination):
    # Anything already under /NavalCore has arrived, even if it was filed somewhere other than
    # this script's destination -- the art folders are meant to be rearranged by hand.
    landed = find_asset_by_name(destination.rpartition("/")[2])
    if landed and landed != destination:
        if unreal.EditorAssetLibrary.does_asset_exist(source):
            warn(f"{source} still exists even though {landed} is already in place; delete one")
        else:
            log(f"Already migrated, filed at {landed}")
        return False

    if unreal.EditorAssetLibrary.does_asset_exist(destination):
        if unreal.EditorAssetLibrary.does_asset_exist(source):
            warn(
                f"{source} and {destination} both exist. Leaving both alone -- delete the "
                "stale one by hand so this cannot pick the wrong asset."
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


def resave_migrated_assets():
    """Rewrite each moved package now that all of its dependencies have moved too.

    A rename saves the package immediately, so a package renamed before the assets it points
    at still names their old locations on disk. Saving writes imports from the live object
    pointers, which are correct, so one more save per asset is the whole fix -- and it is why
    this has to run before the editor is restarted.
    """
    for _, destination in MOVES:
        landed = find_asset_by_name(destination.rpartition("/")[2]) or destination
        asset = (
            unreal.EditorAssetLibrary.load_asset(landed)
            if unreal.EditorAssetLibrary.does_asset_exist(landed)
            else None
        )
        if asset is None:
            continue
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    log(f"Re-saved {len(MOVES)} migrated packages against their final locations")


def redirector_package_path(asset_data):
    """AssetData.package_name is the one spelling every 5.x build agrees on."""
    package_name = getattr(asset_data, "package_name", None)
    if package_name:
        return str(package_name)
    return str(asset_data.get_soft_object_path().to_string()).split(".", 1)[0]


def collect_redirectors():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    redirectors = []
    for root in REDIRECTOR_SCAN_ROOTS:
        for asset_data in registry.get_assets_by_path(root, recursive=True):
            # asset_class_path is the UE5 spelling; asset_class is the older one.
            class_path = getattr(asset_data, "asset_class_path", None)
            class_name = str(class_path.asset_name) if class_path else str(
                getattr(asset_data, "asset_class", "")
            )
            if class_name == "ObjectRedirector":
                redirectors.append(asset_data)
    return redirectors


def fixup_redirectors():
    """Collapse the redirectors the renames left behind. Returns True when none are left.

    AssetTools.fixup_referencers is not exposed to Python in every engine build, so the
    fallback does what it does by hand: loading a referencer resolves the redirector, and
    saving it then writes the destination path. Maps are the exception -- they have to be
    opened and saved, so those are reported rather than touched.
    """
    redirectors = collect_redirectors()
    if not redirectors:
        log("No redirectors left to fix up")
        return True

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    fixup = getattr(asset_tools, "fixup_referencers", None)
    if fixup is not None:
        fixup([data.get_asset() for data in redirectors if data.get_asset() is not None])
        log(f"Fixed up {len(redirectors)} redirectors")
        return True

    unresolved = []
    for data in redirectors:
        redirector_path = redirector_package_path(data)
        referencers = unreal.EditorAssetLibrary.find_package_referencers_for_asset(
            redirector_path, True
        )
        blocked = False
        for referencer in referencers:
            referencer_path = str(referencer)
            referencing_asset = (
                unreal.EditorAssetLibrary.load_asset(referencer_path)
                if unreal.EditorAssetLibrary.does_asset_exist(referencer_path)
                else None
            )
            if referencing_asset is None:
                # A map, or something else that cannot be re-saved from here.
                warn(
                    f"{referencer_path} still points at {redirector_path} and cannot be saved "
                    "from a script. Open it in the editor and save it."
                )
                blocked = True
                continue
            unreal.EditorAssetLibrary.save_loaded_asset(referencing_asset, only_if_is_dirty=False)

        if blocked or not unreal.EditorAssetLibrary.delete_asset(redirector_path):
            unresolved.append(redirector_path)
        else:
            log(f"Resolved and removed the redirector at {redirector_path}")

    if unresolved:
        warn(
            f"{len(unresolved)} redirectors remain: {', '.join(unresolved)}. Right-click "
            "/OceanAdventure and /Raft in the Content Browser and run Fix Up Redirectors In "
            "Folder after saving whatever still points at them."
        )
        return False
    return True


def mesh_materials(mesh):
    getter = getattr(mesh, "get_static_materials", None)
    entries = getter() if getter is not None else mesh.get_editor_property("static_materials")
    for entry in entries or []:
        yield entry.get_editor_property("material_interface")


def verify():
    """Report anything the move left pointing at nothing, by name, so it can be reassigned."""
    problems = []

    cannon_class = (
        unreal.EditorAssetLibrary.load_blueprint_class(CANNON_BLUEPRINT_PATH)
        if unreal.EditorAssetLibrary.does_asset_exist(CANNON_BLUEPRINT_PATH)
        else None
    )
    if cannon_class is None:
        problems.append(f"{CANNON_BLUEPRINT_PATH} is missing")
    else:
        defaults = unreal.get_default_object(cannon_class)
        if defaults.get_editor_property("projectile_class") is None:
            problems.append(f"{CANNON_BLUEPRINT_PATH} has no ProjectileClass")

    for default_path in (CANNON_MESH_PATH, CANNONBALL_MESH_PATH):
        mesh_path = find_asset_by_name(default_path.rpartition("/")[2]) or default_path
        if not unreal.EditorAssetLibrary.does_asset_exist(mesh_path):
            problems.append(f"{mesh_path} is missing")
            continue
        mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
        empty_slots = sum(1 for material in mesh_materials(mesh) if material is None)
        if empty_slots:
            problems.append(f"{mesh_path} has {empty_slots} empty material slot(s)")

    if problems:
        for problem in problems:
            warn(problem)
        warn(
            "Reassign these by hand in the editor, then save. Re-running the migration cannot "
            "recover a reference the editor has already dropped."
        )
    else:
        log("Verified: the shared cannon, its shell and their art all resolve")
    return not problems


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

    repoint_deck_cannon_piece()
    delete_raft_cannon_blueprint()
    resave_migrated_assets()

    # Only once nothing points at a redirector any more is it safe to drop the old folders.
    if fixup_redirectors():
        cleanup_empty_directories()
    else:
        log("Leaving the old folders in place until their redirectors are resolved")

    verify()
    log(
        "Migration done. Re-run CreateNavalCoreCannon.py and the Raft feature's "
        "CreateRaftNavalAssets.py, then open L_NavalP0 and save it."
    )


if __name__ == "__main__":
    main()
