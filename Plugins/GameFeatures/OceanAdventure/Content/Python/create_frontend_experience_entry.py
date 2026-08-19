"""Create the Lyra front-end entry for the Ocean Chunk Test map.

In Lyra the front-end map list is not driven by maps directly. The main screen
asks the AssetManager for every ULyraUserFacingExperienceDefinition primary
asset, loads them, and shows the ones whose bShowInFrontEnd is true. Each of
those points at a map (MapID) and a ULyraExperienceDefinition (ExperienceID)
that declares which Game Feature plugins to activate.

This script creates that pair:

    /OceanAdventure/Experience/BP_Experience_Ocean
        a Blueprint class deriving from ULyraExperienceDefinition
    /OceanAdventure/Experience/DA_Facing_Experience_Ocean
        a ULyraUserFacingExperienceDefinition data asset

The two asset kinds are not interchangeable, and PrimaryAssetTypesToScan says
which is which: LyraExperienceDefinition is scanned with bHasBlueprintClasses
True, so an experience is a Blueprint class, while
LyraUserFacingExperienceDefinition is scanned with it False, so a front-end
tile is a data asset. That is why Lyra's own assets are named BP_Experience_*
and DA_Facing_Experience_*. An experience built as a data asset is invisible to
the class pickers that select one, such as World Settings > Default Gameplay
Experience.

Everything the front-end reaches lives under the OceanAdventure game feature,
which owns this gameplay mode: the experience pair, and the map it launches.
OceanCore stays underneath as the chunk-streaming runtime and exposes no
front-end content of its own.

For the AssetManager to resolve these by primary asset id, /OceanAdventure/Maps
and /OceanAdventure/Experience have to be scanned. Those scan rules belong to
the plugin, declared on its GameFeatureData and applied when the feature
registers -- see Scripts/CreateGameFeatureData.py. Run that first; the project's
Config/DefaultGame.ini deliberately says nothing about /OceanAdventure.

Note that a primary asset id is (type, asset name) — the path is not part of
it. OceanCore ships a map with the same L_OceanChunkTest name, so if it were
ever scanned as well, two assets would claim ("Map", "L_OceanChunkTest") and
the AssetManager would pick one arbitrarily.

Usage:
    From the editor Python console. The console's working directory is the
    engine binaries folder, not the project, so a relative path will not
    resolve. This plugin's Content/Python folder is on sys.path automatically
    (Unreal adds it for every enabled plugin), so import by module name:

        import create_frontend_experience_entry

    Importing runs it. To run again in the same editor session:

        import importlib, create_frontend_experience_entry
        importlib.reload(create_frontend_experience_entry)

    If the import fails, the plugin is not enabled or the editor was started
    before it was added; restart the editor and try again. As a fallback,
    build an absolute path from the project directory:

        import unreal, os
        exec(open(os.path.join(unreal.Paths.project_dir(),
            "Plugins/GameFeatures/OceanAdventure/Content/Python",
            "create_frontend_experience_entry.py")).read())

    Headless:
        UnrealEditor-Cmd <project>.uproject -run=pythonscript -script="<abs path>"

The script is idempotent: existing assets are updated in place.

DefaultPawnData, Actions and ActionSets on the experience are gameplay design
decisions and are deliberately left for you to fill in the editor.
"""

import unreal


MAP_ASSET_PATH = "/OceanAdventure/Maps/L_OceanChunkTest"

EXPERIENCE_PACKAGE_PATH = "/OceanAdventure/Experience"
EXPERIENCE_ASSET_NAME = "BP_Experience_Ocean"
FACING_ASSET_NAME = "DA_Facing_Experience_Ocean"

# The Game Feature plugin to activate with this experience. OceanAdventure is
# the game feature (ExplicitlyLoaded, initial state Registered); it pulls in
# OceanCore and TopDownFeature through its own .uplugin dependencies, so
# OceanCore must not be listed here — it is an always-loaded runtime plugin,
# not a game feature.
GAME_FEATURES_TO_ENABLE = ["OceanAdventure"]

TILE_TITLE = "Ocean Chunk Test"
TILE_SUB_TITLE = "OceanCore"
TILE_DESCRIPTION = "Test map for ocean chunk streaming and island generation."

MAX_PLAYER_COUNT = 4
SHOW_IN_FRONT_END = True

# Also point the map's World Settings at the experience, so opening the map
# directly (PIE straight into it, or a server opening it) runs the experience
# without going through the front end. This opens and saves the map, so save
# anything unsaved in the level you have open before running.
SET_MAP_DEFAULT_EXPERIENCE = True


def log(message):
    unreal.log(f"[OceanFrontendEntry] {message}")


def warn(message):
    unreal.log_warning(f"[OceanFrontendEntry] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def make_primary_asset_id(asset_type, asset_name):
    # Struct construction from keywords is not available on every engine build,
    # so fall back to default-constructing and assigning the two fields.
    type_value = unreal.PrimaryAssetType(asset_type)
    name_value = unreal.Name(asset_name)
    try:
        return unreal.PrimaryAssetId(
            primary_asset_type=type_value, primary_asset_name=name_value
        )
    except (TypeError, ValueError):
        asset_id = unreal.PrimaryAssetId()
        asset_id.set_editor_property("primary_asset_type", type_value)
        asset_id.set_editor_property("primary_asset_name", name_value)
        return asset_id


def load_existing(asset_path, expected_class):
    """Return the asset at asset_path, or None. Rejects a wrong-typed leftover."""
    if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return None

    existing = require(
        unreal.EditorAssetLibrary.load_asset(asset_path),
        f"Unable to load asset: {asset_path}",
    )
    if not isinstance(existing, expected_class):
        raise RuntimeError(
            f"{asset_path} already exists as {type(existing).__name__}, expected "
            f"{expected_class.__name__}. An earlier version of this script created "
            "the experience as a data asset instead of a Blueprint class. Delete "
            "the asset in the editor and run this again."
        )
    log(f"Updating existing asset: {asset_path}")
    return existing


def get_or_create_data_asset(asset_name, package_path, asset_class):
    asset_path = f"{package_path}/{asset_name}"
    existing = load_existing(asset_path, asset_class)
    if existing is not None:
        return existing

    log(f"Creating data asset: {asset_path} ({asset_class.__name__})")
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    return require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, asset_class, factory
        ),
        f"Unable to create asset: {asset_path}",
    )


def get_or_create_blueprint(asset_name, package_path, parent_class):
    """Create a Blueprint class asset deriving from parent_class.

    ULyraExperienceDefinition is scanned with bHasBlueprintClasses=True, so an
    experience must be a Blueprint class (the BP_ prefix in Lyra's own assets),
    not a data asset instance. A data asset would never appear in the class
    pickers that select an experience.
    """
    asset_path = f"{package_path}/{asset_name}"
    existing = load_existing(asset_path, unreal.Blueprint)
    if existing is not None:
        return existing

    log(f"Creating Blueprint class: {asset_path} (parent {parent_class.__name__})")
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    return require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, unreal.Blueprint, factory
        ),
        f"Unable to create asset: {asset_path}",
    )


def verify_map():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_ASSET_PATH):
        log(f"Map found: {MAP_ASSET_PATH}")
        return True

    warn(
        f"Map not found: {MAP_ASSET_PATH}. The front-end tile will be created but "
        "MapID will not resolve until the map exists."
    )
    return False


def create_experience_definition():
    experience = get_or_create_blueprint(
        EXPERIENCE_ASSET_NAME, EXPERIENCE_PACKAGE_PATH, unreal.LyraExperienceDefinition
    )

    # Settings on a Blueprint class live on its class default object.
    generated_class = require(
        experience.generated_class(),
        f"{EXPERIENCE_ASSET_NAME} has no generated class; the Blueprint failed to "
        "compile.",
    )
    defaults = require(
        unreal.get_default_object(generated_class),
        f"Unable to reach the class defaults of {EXPERIENCE_ASSET_NAME}",
    )
    defaults.set_editor_property("game_features_to_enable", GAME_FEATURES_TO_ENABLE)

    unreal.BlueprintEditorLibrary.compile_blueprint(experience)
    require(
        unreal.EditorAssetLibrary.save_loaded_asset(experience),
        f"Unable to save {EXPERIENCE_ASSET_NAME}",
    )
    log(f"{EXPERIENCE_ASSET_NAME}: GameFeaturesToEnable = {GAME_FEATURES_TO_ENABLE}")
    log(
        f"{EXPERIENCE_ASSET_NAME}: set DefaultPawnData / Actions / ActionSets in the "
        "editor before playing."
    )
    return experience


def set_map_default_experience(generated_class):
    """Point the map's ALyraWorldSettings at the experience class."""
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    current_world = editor_subsystem.get_editor_world()
    current_map_path = (
        current_world.get_path_name().split(".", 1)[0] if current_world else None
    )
    if current_map_path != MAP_ASSET_PATH:
        require(
            level_subsystem.load_level(MAP_ASSET_PATH),
            f"Unable to load {MAP_ASSET_PATH}",
        )
        current_world = editor_subsystem.get_editor_world()

    world_settings = require(
        unreal.GameplayStatics.get_world_settings(current_world),
        f"Unable to reach the World Settings of {MAP_ASSET_PATH}",
    )
    if not isinstance(world_settings, unreal.LyraWorldSettings):
        warn(
            f"{MAP_ASSET_PATH} uses {type(world_settings).__name__}, not "
            "LyraWorldSettings, so it has no DefaultGameplayExperience. Set the "
            "World Settings class in Project Settings > Engine > General Settings."
        )
        return

    world_settings.set_editor_property("default_gameplay_experience", generated_class)
    require(level_subsystem.save_current_level(), f"Unable to save {MAP_ASSET_PATH}")
    log(f"{MAP_ASSET_PATH}: DefaultGameplayExperience = {EXPERIENCE_ASSET_NAME}")


def create_user_facing_experience():
    facing = get_or_create_data_asset(
        FACING_ASSET_NAME,
        EXPERIENCE_PACKAGE_PATH,
        unreal.LyraUserFacingExperienceDefinition,
    )

    map_asset_name = MAP_ASSET_PATH.rsplit("/", 1)[-1]
    facing.set_editor_property("map_id", make_primary_asset_id("Map", map_asset_name))
    facing.set_editor_property(
        "experience_id",
        make_primary_asset_id("LyraExperienceDefinition", EXPERIENCE_ASSET_NAME),
    )
    facing.set_editor_property("tile_title", TILE_TITLE)
    facing.set_editor_property("tile_sub_title", TILE_SUB_TITLE)
    facing.set_editor_property("tile_description", TILE_DESCRIPTION)
    facing.set_editor_property("max_player_count", MAX_PLAYER_COUNT)
    facing.set_editor_property("show_in_front_end", SHOW_IN_FRONT_END)

    require(
        unreal.EditorAssetLibrary.save_loaded_asset(facing),
        f"Unable to save {FACING_ASSET_NAME}",
    )
    log(f"{FACING_ASSET_NAME}: bShowInFrontEnd = {SHOW_IN_FRONT_END}")
    return facing


def main():
    map_exists = verify_map()
    experience = create_experience_definition()
    create_user_facing_experience()

    if SET_MAP_DEFAULT_EXPERIENCE and map_exists:
        set_map_default_experience(experience.generated_class())

    log("Done. Restart the editor (or PIE into the front-end) to see the new tile.")
    if not map_exists:
        warn("Reminder: the map is still missing, so the tile will fail to launch.")


main()
