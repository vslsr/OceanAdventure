"""Create the Raft GameFeatureData and its primary asset scan rule.

Run after compiling RaftRuntime:

    py "<project>/Plugins/GameFeatures/Raft/Content/Python/CreateRaftGameFeatureData.py"
"""

import unreal


ASSET_PATH = "/Raft/Raft"


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def log(message):
    unreal.log(f"[RaftGameFeatureData] {message}")


def make_directory_path(path):
    directory = unreal.DirectoryPath()
    directory.set_editor_property("path", path)
    return directory


def get_or_create_game_feature_data():
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        return require(
            unreal.EditorAssetLibrary.load_asset(ASSET_PATH),
            f"Unable to load {ASSET_PATH}",
        )

    package_path, asset_name = ASSET_PATH.rsplit("/", 1)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.GameFeatureData)
    return require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, unreal.GameFeatureData, factory
        ),
        f"Unable to create {ASSET_PATH}",
    )


def main():
    definition_class = getattr(unreal, "RaftDefinition", None)
    require(definition_class, "RaftDefinition is unavailable; compile RaftRuntime first")

    game_feature_data = get_or_create_game_feature_data()
    type_info = unreal.PrimaryAssetTypeInfo()
    type_info.set_editor_property("primary_asset_type", unreal.Name("RaftDefinition"))
    type_info.set_editor_property("asset_base_class", definition_class)
    type_info.set_editor_property("has_blueprint_classes", False)
    type_info.set_editor_property("is_editor_only", False)
    type_info.set_editor_property(
        "directories", [make_directory_path("/Raft/Vehicles")]
    )

    game_feature_data.set_editor_property("primary_asset_types_to_scan", [type_info])
    game_feature_data.set_editor_property("actions", [])
    require(
        unreal.EditorAssetLibrary.save_asset(ASSET_PATH, only_if_is_dirty=False),
        f"Unable to save {ASSET_PATH}",
    )
    log(f"Configured {ASSET_PATH}")


main()
