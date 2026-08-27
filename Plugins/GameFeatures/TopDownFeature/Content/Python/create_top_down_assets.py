import unreal


PLUGIN_ROOT = "/TopDownFeature"
INPUT_ROOT = f"{PLUGIN_ROOT}/Input"
PAWN_ROOT = f"{PLUGIN_ROOT}/Pawn"
BASE_INPUT_CONFIG_PATH = "/Game/Input/DA_InputConfig_Base"
DEFAULT_PAWN_CLASS_PATH = (
    "/Game/Character/BP_Character_Hero_Default.BP_Character_Hero_Default_C"
)
LEGACY_INPUT_ASSET_PATHS = (
    f"{INPUT_ROOT}/IMC_TopDown",
    f"{INPUT_ROOT}/DA_TopDown_InputConfig",
)
LEGACY_GAS_ACTION_NAMES = {
    "TopDown_AddComponents",
    "TopDown_AddInputMapping",
    "TopDown_AddInputBinding",
    "TopDown_AddAbilities",
}
LEGACY_INPUT_ACTION_CLASSES = {
    "GameFeatureAction_AddInputContextMapping",
    "GameFeatureAction_AddInputBinding",
}


def scan_asset_directory(package_path):
    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
    asset_registry.scan_paths_synchronous([package_path], True, True)


def load_existing(asset_path):
    package_path = asset_path.rsplit("/", 1)[0]
    scan_asset_directory(package_path)
    if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return None
    return unreal.EditorAssetLibrary.load_asset(asset_path)


def load_or_create_data_asset(asset_name, package_path, asset_class):
    asset_path = f"{package_path}/{asset_name}"
    scan_asset_directory(package_path)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return unreal.EditorAssetLibrary.load_asset(asset_path)

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name,
        package_path,
        asset_class,
        factory,
    )


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def is_legacy_input_action(action):
    if action is None:
        return False

    class_name = str(action.get_class().get_name())
    # Actions stored in UGameFeatureData::Actions arrive in Python as the base
    # GameFeatureAction wrapper. get_class() reports the concrete type, but UE 5.7 cannot
    # read derived properties such as InputMappings through that wrapper. Under the current
    # ownership model this feature must not inject any mapping or InputConfig, so the
    # concrete action class is the complete and stable migration key.
    return (
        str(action.get_name()) in LEGACY_GAS_ACTION_NAMES
        or class_name in LEGACY_INPUT_ACTION_CLASSES
    )


def save_asset(asset):
    asset_path = asset.get_path_name().split(".", 1)[0]
    require(
        unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=True),
        f"Failed to save {asset_path}.",
    )


game_feature_data = require(
    load_or_create_data_asset("TopDownFeature", PLUGIN_ROOT, unreal.GameFeatureData),
    "Failed to create TopDownFeature GameFeatureData.",
)

pawn_data_class = require(
    unreal.load_class(None, "/Script/LyraGame.LyraPawnData"),
    "Failed to load ULyraPawnData.",
)
pawn_data = require(
    load_or_create_data_asset(
        "DA_TopDown_PawnData",
        PAWN_ROOT,
        pawn_data_class,
    ),
    "Failed to create DA_TopDown_PawnData.",
)
pawn_class = require(
    unreal.load_class(None, DEFAULT_PAWN_CLASS_PATH),
    f"Failed to load the default project pawn class: {DEFAULT_PAWN_CLASS_PATH}",
)
camera_class = require(
    unreal.load_class(None, "/Script/TopDownFeatureRuntime.LyraCameraMode_TopDownFollow"),
    "Failed to load ULyraCameraMode_TopDownFollow.",
)
base_input_config = require(
    load_existing(BASE_INPUT_CONFIG_PATH),
    f"Failed to load the project base InputConfig: {BASE_INPUT_CONFIG_PATH}",
)
pawn_data.set_editor_property("pawn_class", pawn_class)
pawn_data.set_editor_property("ability_sets", [])
pawn_data.set_editor_property("input_config", base_input_config)
pawn_data.set_editor_property("default_camera_mode", camera_class)

# Input assets now belong to the gameplay feature that owns the player Pawn. Remove only
# actions created by the rejected GAS experiment or actions that reference these two exact
# TopDown legacy assets; unrelated hand-authored actions remain untouched.
game_feature_data.set_editor_property(
    "actions",
    [
        action
        for action in game_feature_data.get_editor_property("actions")
        if action is not None and not is_legacy_input_action(action)
    ],
)
require(
    not any(is_legacy_input_action(action) for action in game_feature_data.get_editor_property("actions")),
    "TopDownFeature still references a legacy TopDown input asset.",
)

assets_to_save = [game_feature_data, pawn_data]
for asset in assets_to_save:
    save_asset(asset)

for legacy_asset_path in LEGACY_INPUT_ASSET_PATHS:
    scan_asset_directory(legacy_asset_path.rsplit("/", 1)[0])
    if unreal.EditorAssetLibrary.does_asset_exist(legacy_asset_path):
        remaining_referencers = unreal.EditorAssetLibrary.find_package_referencers_for_asset(
            legacy_asset_path,
            True,
        )
        require(
            not remaining_referencers,
            f"Refusing to delete {legacy_asset_path}; remaining referencers: "
            f"{remaining_referencers}",
        )
        require(
            unreal.EditorAssetLibrary.delete_asset(legacy_asset_path),
            f"Failed to delete legacy input asset {legacy_asset_path}.",
        )
    require(
        not unreal.EditorAssetLibrary.does_asset_exist(legacy_asset_path),
        f"Legacy input asset still exists after migration: {legacy_asset_path}",
    )

unreal.log_warning("TOPDOWN_ASSETS_MIGRATED")
unreal.log_warning("TopDownFeature no longer injects or owns an InputMappingContext/InputConfig.")
unreal.log_warning("The consuming player GameFeature must own those assets and inject UTopDownPawnComponent.")
