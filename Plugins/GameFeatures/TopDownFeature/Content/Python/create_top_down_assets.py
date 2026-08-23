import unreal


PLUGIN_ROOT = "/TopDownFeature"
INPUT_ROOT = f"{PLUGIN_ROOT}/Input"
PAWN_ROOT = f"{PLUGIN_ROOT}/Pawn"
DEFAULT_PAWN_CLASS_PATH = (
    "/Game/Character/BP_Character_Hero_Default.BP_Character_Hero_Default_C"
)


def scan_asset_directory(package_path):
    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
    asset_registry.scan_paths_synchronous([package_path], True)


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


def load_or_duplicate(source_path, destination_path):
    destination_package_path = destination_path.rsplit("/", 1)[0]
    scan_asset_directory(destination_package_path)
    if unreal.EditorAssetLibrary.does_asset_exist(destination_path):
        return unreal.EditorAssetLibrary.load_asset(destination_path)

    source_package_path = source_path.rsplit("/", 1)[0]
    scan_asset_directory(source_package_path)
    return unreal.EditorAssetLibrary.duplicate_asset(source_path, destination_path)


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


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

input_action_specs = [
    ("IA_TopDownClick", unreal.InputActionValueType.BOOLEAN, "click_input_tag", "LeftMouseButton"),
    ("IA_TopDownCameraZoom", unreal.InputActionValueType.AXIS1D, "camera_zoom_input_tag", "MouseWheelAxis"),
    ("IA_TopDownCameraRotateHold", unreal.InputActionValueType.BOOLEAN, "camera_rotate_hold_input_tag", "RightMouseButton"),
    ("IA_TopDownCameraRotate", unreal.InputActionValueType.AXIS2D, "camera_rotate_input_tag", "Mouse2D"),
]

input_actions = []
for asset_name, value_type, _, _ in input_action_specs:
    action = require(
        load_or_create_data_asset(asset_name, INPUT_ROOT, unreal.InputAction),
        f"Failed to create {asset_name}.",
    )
    action.set_editor_property("value_type", value_type)
    input_actions.append(action)

input_mapping = require(
    load_or_create_data_asset("IMC_TopDown", INPUT_ROOT, unreal.InputMappingContext),
    "Failed to create IMC_TopDown.",
)
for action, (_, _, _, key_name) in zip(input_actions, input_action_specs):
    input_mapping.unmap_all_keys_from_action(action)
    key = unreal.Key()
    key.set_editor_property("key_name", unreal.Name(key_name))
    input_mapping.map_key(action, key)

input_config = require(
    load_or_duplicate(
        "/Game/Input/DA_InputConfig_Base",
        f"{INPUT_ROOT}/DA_TopDown_InputConfig",
    ),
    "Failed to duplicate the base Lyra InputConfig.",
)
component_class = require(
    unreal.load_class(None, "/Script/TopDownFeatureRuntime.TopDownPawnComponent"),
    "Failed to load UTopDownPawnComponent.",
)
component_cdo = unreal.get_default_object(component_class)
owned_tags = [
    require(
        component_cdo.get_editor_property(tag_property),
        f"UTopDownPawnComponent has no valid {tag_property}.",
    )
    for _, _, tag_property, _ in input_action_specs
]
native_actions = [
    action
    for action in input_config.get_editor_property("native_input_actions")
    if action.get_editor_property("input_tag") not in owned_tags
]
for action, input_tag in zip(input_actions, owned_tags):
    native_actions.append(unreal.LyraInputAction(input_action=action, input_tag=input_tag))
input_config.set_editor_property("native_input_actions", native_actions)

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
pawn_data.set_editor_property("pawn_class", pawn_class)
pawn_data.set_editor_property("ability_sets", [])
pawn_data.set_editor_property("input_config", input_config)
pawn_data.set_editor_property("default_camera_mode", camera_class)

assets_to_save = [game_feature_data, *input_actions, input_mapping, input_config, pawn_data]
for asset in assets_to_save:
    save_asset(asset)

unreal.log_warning("TOPDOWN_ASSETS_CREATED")
unreal.log_warning("DA_TopDown_PawnData intentionally has no AbilitySets; add gameplay-specific sets in the consuming feature.")
unreal.log_warning("Configure Add Components and Add Input Mapping actions on /TopDownFeature/TopDownFeature before activation.")
