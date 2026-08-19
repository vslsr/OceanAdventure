"""Create and configure the OceanAdventure GameFeatureData asset.

A game feature declares its own primary asset scan rules on its
GameFeatureData. The GameFeatures subsystem hands them to the AssetManager
when the plugin reaches the Registered state, which happens at startup for a
plugin with BuiltInInitialFeatureState "Registered" -- well before any
experience loads and activates the feature. So the plugin's content is
discoverable without the project's Config/DefaultGame.ini knowing anything
about /OceanAdventure, which keeps the plugin self-contained: remove the
plugin and its scan rules leave with it.

This mirrors what SimpleExperience already does.

Run from the editor Python console:
    import CreateGameFeatureData

Idempotent: the asset is created if missing, and the scan rules are rewritten
to match SCAN_TYPES either way.
"""

import unreal


ASSET_NAME = "OceanAdventure"
PACKAGE_PATH = "/OceanAdventure"
ASSET_PATH = f"{PACKAGE_PATH}/{ASSET_NAME}"

# (primary asset type, base class, has blueprint classes, directories)
#
# Experiences are Blueprint classes, so LyraExperienceDefinition is scanned
# with blueprint classes on; the front-end tile is a data asset, so
# LyraUserFacingExperienceDefinition is scanned with it off.
SCAN_TYPES = [
    (
        "Map",
        "/Script/Engine.World",
        False,
        ["/OceanAdventure/Maps"],
    ),
    (
        "LyraExperienceDefinition",
        "/Script/LyraGame.LyraExperienceDefinition",
        True,
        ["/OceanAdventure/Experience"],
    ),
    (
        "LyraUserFacingExperienceDefinition",
        "/Script/LyraGame.LyraUserFacingExperienceDefinition",
        False,
        ["/OceanAdventure/Experience"],
    ),
]


def log(message):
    unreal.log(f"[OceanAdventureGameFeatureData] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def make_directory_path(path):
    directory = unreal.DirectoryPath()
    directory.set_editor_property("path", path)
    return directory


def make_type_info(type_name, base_class_path, has_blueprint_classes, directories):
    info = unreal.PrimaryAssetTypeInfo()
    info.set_editor_property("primary_asset_type", unreal.Name(type_name))
    info.set_editor_property("asset_base_class", unreal.SoftClassPath(base_class_path))
    info.set_editor_property("has_blueprint_classes", has_blueprint_classes)
    info.set_editor_property("is_editor_only", False)
    info.set_editor_property(
        "directories", [make_directory_path(path) for path in directories]
    )
    return info


def get_or_create_game_feature_data():
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        log(f"GameFeatureData already exists: {ASSET_PATH}")
        return require(
            unreal.EditorAssetLibrary.load_asset(ASSET_PATH),
            f"Unable to load GameFeatureData: {ASSET_PATH}",
        )

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.GameFeatureData)
    asset = require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            ASSET_NAME, PACKAGE_PATH, unreal.GameFeatureData, factory
        ),
        f"Failed to create GameFeatureData: {ASSET_PATH}",
    )
    log(f"Created GameFeatureData: {ASSET_PATH}")
    return asset


def main():
    game_feature_data = get_or_create_game_feature_data()

    game_feature_data.set_editor_property(
        "primary_asset_types_to_scan",
        [make_type_info(*entry) for entry in SCAN_TYPES],
    )
    require(
        unreal.EditorAssetLibrary.save_asset(ASSET_PATH, only_if_is_dirty=False),
        f"Failed to save GameFeatureData: {ASSET_PATH}",
    )

    for type_name, _, _, directories in SCAN_TYPES:
        log(f"Scanning {type_name} in {', '.join(directories)}")
    log("Done. Restart the editor so the AssetManager picks up the new scan rules.")


main()
