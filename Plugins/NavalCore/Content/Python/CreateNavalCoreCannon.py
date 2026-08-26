"""Author the one shared cannon that both the field emplacement and the deck gun use.

Design 7.10 is explicit that ground and mounted are the same weapon under the same rules --
only the support around it differs. That has always been true of the C++ (`ANavalHeavyWeaponActor`
reads its team from the vessel it is attached to, or from its deploy call when it stands on
the ground), but the content used to be split into one Blueprint per GameFeature, which meant
two guns that could silently drift apart. They did: the deck copy had no mesh, no projectile
and the C++ default minimum range.

So the gun lives here, in the general framework plugin, and both features point at it:

    /NavalCore/Naval/BP_Naval_Cannon                 the shared gun
    /NavalCore/Naval/BP_Naval_CannonballProjectile   its shell
    /NavalCore/Naval/Meshes/SM_Naval_Cannon          the gun's art
    /NavalCore/Naval/Meshes/SM_Naval_Cannonball      the shell's art

This is the only direction the layering allows. A GameFeature may not reference another
GameFeature's assets, and NavalCore may not reference a GameFeature's -- but every feature is
free to depend on the general framework, so this is the one place both can meet.

Nothing here may reference /OceanAdventure, /Raft or any other feature path.

Run once after enabling content on the NavalCore plugin, and after MigrateSharedCannon.py has
moved the existing art over. Safe to re-run.

    import CreateNavalCoreCannon
    CreateNavalCoreCannon.main()
"""

from pathlib import Path

import unreal


PLUGIN_ROOT = "/NavalCore"
NAVAL_ROOT = f"{PLUGIN_ROOT}/Naval"
MESH_ROOT = f"{NAVAL_ROOT}/Meshes"

CANNON_BLUEPRINT_PATH = f"{NAVAL_ROOT}/BP_Naval_Cannon"
PROJECTILE_BLUEPRINT_PATH = f"{NAVAL_ROOT}/BP_Naval_CannonballProjectile"
CANNON_MESH_PATH = f"{MESH_ROOT}/SM_Naval_Cannon"
CANNONBALL_MESH_PATH = f"{MESH_ROOT}/SM_Naval_Cannonball"

PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
BLENDER_MODELS = PROJECT_ROOT / "blender" / "models"
CANNONBALL_SOURCE_FBX = BLENDER_MODELS / "SM_Naval_Cannonball.fbx"
CANNON_SOURCE_FBX = BLENDER_MODELS / "SM_Naval_Cannon.fbx"

# Tuning for the shared gun. Edit here and re-run, or change the same properties directly on
# BP_Naval_Cannon. Blueprint overrides beat the C++ defaults, so a value that matters has to
# be written here -- otherwise a stale override keeps the old arc after a C++ change.
CANNON_TRAJECTORY_DEFAULTS = {
    # Keep the initial preview and the shortest shot close to the gun, while leaving enough
    # clearance for the muzzle/occupant collision volume.
    "minimum_range": 400.0,
    "max_range": 7000.0,
    "trajectory_flight_seconds": 2.4,
    "max_trajectory_rise": 600.0,
}


def log(message):
    unreal.log(f"[NavalCoreCannon] {message}")


def warn(message):
    unreal.log_warning(f"[NavalCoreCannon] {message}")


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


def get_or_create_blueprint(asset_path, parent_class):
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return require(
            unreal.EditorAssetLibrary.load_asset(asset_path), f"Unable to load {asset_path}"
        )

    package_path, asset_name = split_path(asset_path)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    return require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, unreal.Blueprint, factory
        ),
        f"Unable to create {asset_path}",
    )


def blueprint_class(blueprint, asset_path):
    generated = blueprint.generated_class()
    if generated is not None:
        return generated
    return require(
        unreal.EditorAssetLibrary.load_blueprint_class(asset_path),
        f"Unable to resolve the generated class of {asset_path}",
    )


def import_or_reuse_mesh(asset_path, source_fbx):
    """Re-import from blender/models when the source is there, else keep what exists.

    A content-only checkout has no FBX, and the cannon's own source has never been exported
    (blender/cannon.blend exists, blender/models/SM_Naval_Cannon.fbx does not), so a missing
    source is normal rather than an error -- the asset moved here by the migration is used
    as-is. Re-importing on every run is what stops a Blender revision from being ignored.
    """
    existing = (
        unreal.EditorAssetLibrary.load_asset(asset_path)
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path)
        else None
    )
    if not source_fbx.is_file():
        return existing

    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    options.get_editor_property("static_mesh_import_data").set_editor_property("combine_meshes", True)

    destination_path, destination_name = split_path(asset_path)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source_fbx))
    task.set_editor_property("destination_path", destination_path)
    task.set_editor_property("destination_name", destination_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    mesh = require(
        unreal.EditorAssetLibrary.load_asset(asset_path), f"Unable to import {asset_path}"
    )
    log(f"Imported {source_fbx.name} into {asset_path}")
    return mesh


def configure_projectile_blueprint(mesh):
    projectile_parent = require(
        unreal.load_class(None, "/Script/NavalCoreRuntime.NavalProjectile"),
        "Failed to load ANavalProjectile; compile NavalCoreRuntime first",
    )
    projectile = get_or_create_blueprint(PROJECTILE_BLUEPRINT_PATH, projectile_parent)
    unreal.BlueprintEditorLibrary.compile_blueprint(projectile)

    if mesh is not None:
        defaults = unreal.get_default_object(blueprint_class(projectile, PROJECTILE_BLUEPRINT_PATH))
        mesh_component = require(
            defaults.get_editor_property("projectile_mesh"),
            "ANavalProjectile has no ProjectileMesh component",
        )
        mesh_component.set_static_mesh(mesh)
        mesh_component.set_relative_scale3d(unreal.Vector(1.0, 1.0, 1.0))
        unreal.BlueprintEditorLibrary.compile_blueprint(projectile)

    save(projectile)

    configured_class = blueprint_class(projectile, PROJECTILE_BLUEPRINT_PATH)
    if mesh is not None:
        configured_mesh = unreal.get_default_object(configured_class).get_editor_property(
            "projectile_mesh"
        ).get_editor_property("static_mesh")
        require(
            package_of(configured_mesh) == CANNONBALL_MESH_PATH,
            "Projectile Blueprint did not retain the cannonball static mesh",
        )
    return configured_class


def configure_cannon_blueprint(projectile_class):
    """The gun itself: ballistics and the shell it fires.

    Meshes are deliberately not written here. The cannon's art is assigned by hand in this
    Blueprint, and a script that reassigns it would either fight that authoring or duplicate
    the model next to it.
    """
    weapon_class = require(
        unreal.load_class(None, "/Script/NavalCoreRuntime.NavalHeavyWeaponActor"),
        "Failed to load ANavalHeavyWeaponActor; compile NavalCoreRuntime first",
    )
    cannon = get_or_create_blueprint(CANNON_BLUEPRINT_PATH, weapon_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(cannon)

    defaults = unreal.get_default_object(blueprint_class(cannon, CANNON_BLUEPRINT_PATH))
    defaults.set_editor_property("projectile_class", projectile_class)
    for property_name, value in CANNON_TRAJECTORY_DEFAULTS.items():
        defaults.set_editor_property(property_name, value)
    unreal.BlueprintEditorLibrary.compile_blueprint(cannon)
    save(cannon)

    configured = unreal.get_default_object(blueprint_class(cannon, CANNON_BLUEPRINT_PATH))
    require(
        package_of(configured.get_editor_property("projectile_class"))
        == package_of(projectile_class),
        "Cannon did not retain BP_Naval_CannonballProjectile as ProjectileClass",
    )
    for property_name, expected_value in CANNON_TRAJECTORY_DEFAULTS.items():
        retained = float(configured.get_editor_property(property_name))
        require(
            abs(retained - expected_value) <= 0.01,
            f"Cannon did not retain {property_name}={expected_value}; got {retained}",
        )

    minimum_range = float(configured.get_editor_property("minimum_range"))
    max_range = float(configured.get_editor_property("max_range"))
    require(
        minimum_range < max_range,
        f"Cannon MinimumRange={minimum_range} is not below MaxRange={max_range}; charging "
        "would pull the impact point inwards instead of pushing it out",
    )
    log(
        f"Configured {CANNON_BLUEPRINT_PATH}: range {minimum_range:.0f}-{max_range:.0f}cm, "
        f"apex at full charge {float(configured.get_editor_property('max_trajectory_rise')):.0f}cm"
    )
    return blueprint_class(cannon, CANNON_BLUEPRINT_PATH)


def main():
    require(
        unreal.EditorAssetLibrary.does_directory_exist(PLUGIN_ROOT),
        f"{PLUGIN_ROOT} is not mounted. Set \"CanContainContent\": true in NavalCore.uplugin "
        "and restart the editor.",
    )
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([PLUGIN_ROOT], True, True)

    cannonball_mesh = import_or_reuse_mesh(CANNONBALL_MESH_PATH, CANNONBALL_SOURCE_FBX)
    if cannonball_mesh is None:
        warn(
            f"No cannonball mesh at {CANNONBALL_MESH_PATH} and no {CANNONBALL_SOURCE_FBX.name} "
            "in blender/models. Run blender/script/python/create_naval_cannonball.py, or "
            "MigrateSharedCannon.py if the asset is still under a GameFeature."
        )

    cannon_mesh = import_or_reuse_mesh(CANNON_MESH_PATH, CANNON_SOURCE_FBX)
    if cannon_mesh is None:
        warn(
            f"No cannon mesh at {CANNON_MESH_PATH}. The gun will be invisible until one is "
            "assigned by hand in BP_Naval_Cannon; export blender/cannon.blend to "
            f"blender/models/{CANNON_SOURCE_FBX.name} to have this script import it."
        )

    projectile_class = configure_projectile_blueprint(cannonball_mesh)
    configure_cannon_blueprint(projectile_class)

    log(
        "The shared cannon is authored. Point the field emplacement at it through "
        "OceanAdventureNavalSettings.GroundHeavyWeaponClass, and the deck piece through "
        "the Raft feature's CreateRaftNavalAssets.py."
    )


if __name__ == "__main__":
    main()
