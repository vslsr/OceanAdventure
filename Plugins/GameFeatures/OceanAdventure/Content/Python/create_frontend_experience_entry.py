"""Create the Lyra front-end entry for the Ocean Chunk Test map.

In Lyra the front-end map list is not driven by maps directly. The main screen
asks the AssetManager for every ULyraUserFacingExperienceDefinition primary
asset, loads them, and shows the ones whose bShowInFrontEnd is true. Each of
those points at a map (MapID) and a ULyraExperienceDefinition (ExperienceID)
that declares which Game Feature plugins to activate.

This script creates that pair:

    /OceanAdventure/Experience/BP_Experience_Ocean         (ULyraExperienceDefinition)
    /OceanAdventure/Experience/DA_Facing_Experience_Ocean  (ULyraUserFacingExperienceDefinition)

Everything the front-end reaches lives under the OceanAdventure game feature,
which owns this gameplay mode: the experience pair, and the map it launches.
OceanCore stays underneath as the chunk-streaming runtime and exposes no
front-end content of its own.

That works because Config/DefaultGame.ini lists /OceanAdventure/Experience in
the LyraExperienceDefinition and LyraUserFacingExperienceDefinition scan rules,
and /OceanAdventure/Maps in the Map rule. Without those scan directories the
assets resolve in the editor (via bShouldGuessTypeAndNameInEditor) but go
missing in a packaged build.

Note that a primary asset id is (type, asset name) — the path is not part of
it. OceanCore ships a map with the same L_OceanChunkTest name, so scanning both
/OceanCore/Maps and /OceanAdventure/Maps would make two assets claim the id
("Map", "L_OceanChunkTest") and the AssetManager would pick one arbitrarily.
Only the OceanAdventure map is scanned, which keeps the id unambiguous.

Usage:
    Run from the editor Python console:
        exec(open(r"Plugins/GameFeatures/OceanAdventure/Content/Python/create_frontend_experience_entry.py").read())
    Or headless:
        UnrealEditor-Cmd <project>.uproject -run=pythonscript -script="<this file>"

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


def log(message):
    unreal.log(f"[OceanFrontendEntry] {message}")


def warn(message):
    unreal.log_warning(f"[OceanFrontendEntry] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def make_primary_asset_id(asset_type, asset_name):
    return unreal.PrimaryAssetId(
        primary_asset_type=unreal.PrimaryAssetType(asset_type),
        primary_asset_name=unreal.Name(asset_name),
    )


def get_or_create_data_asset(asset_name, package_path, asset_class):
    asset_path = f"{package_path}/{asset_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        log(f"Updating existing asset: {asset_path}")
        return require(
            unreal.EditorAssetLibrary.load_asset(asset_path),
            f"Unable to load asset: {asset_path}",
        )

    log(f"Creating asset: {asset_path} ({asset_class.get_name()})")
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    return require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, asset_class, factory
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
    experience = get_or_create_data_asset(
        EXPERIENCE_ASSET_NAME, EXPERIENCE_PACKAGE_PATH, unreal.LyraExperienceDefinition
    )
    experience.set_editor_property("game_features_to_enable", GAME_FEATURES_TO_ENABLE)
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
    facing.set_editor_property("tile_title", unreal.Text(TILE_TITLE))
    facing.set_editor_property("tile_sub_title", unreal.Text(TILE_SUB_TITLE))
    facing.set_editor_property("tile_description", unreal.Text(TILE_DESCRIPTION))
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
    create_experience_definition()
    create_user_facing_experience()

    log("Done. Restart the editor (or PIE into the front-end) to see the new tile.")
    if not map_exists:
        warn("Reminder: the map is still missing, so the tile will fail to launch.")


main()
