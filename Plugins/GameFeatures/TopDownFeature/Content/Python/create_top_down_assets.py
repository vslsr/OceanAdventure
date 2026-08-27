import unreal


PLUGIN_ROOT = "/TopDownFeature"
INPUT_ROOT = f"{PLUGIN_ROOT}/Input"
PAWN_ROOT = f"{PLUGIN_ROOT}/Pawn"
ABILITY_ROOT = f"{PLUGIN_ROOT}/Abilities"
MOVE_ABILITY_CLASS_PATH = "/Script/TopDownFeatureRuntime.TopDownGameplayAbility_Move"
OWNED_ACTION_CLASSES = {
    "TopDown_AddComponents": "GameFeatureAction_AddComponents",
    "TopDown_AddInputMapping": "GameFeatureAction_AddInputContextMapping",
    "TopDown_AddInputBinding": "GameFeatureAction_AddInputBinding",
    "TopDown_AddAbilities": "GameFeatureAction_AddAbilities",
}
DEFAULT_PAWN_CLASS_PATH = (
    "/Game/Character/BP_Character_Hero_Default.BP_Character_Hero_Default_C"
)


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


def asset_path(asset):
    """Return a stable asset path for UE Python wrappers (object equality is unreliable)."""
    if asset is None:
        return ""
    get_path_name = getattr(asset, "get_path_name", None)
    if get_path_name is None:
        return str(asset)
    return str(get_path_name()).split(".", 1)[0]


def gameplay_tags_equal(left, right):
    """Compare FGameplayTag values; Python's generated ``==`` wrapper compares identity."""
    equal_tag = getattr(unreal.GameplayTagLibrary, "equal_equal_gameplay_tag", None)
    if equal_tag is not None:
        return bool(equal_tag(left, right))

    export_left = getattr(left, "export_text", None)
    export_right = getattr(right, "export_text", None)
    if export_left is not None and export_right is not None:
        return str(export_left()) == str(export_right())
    return left == right


def has_input_action(entries, action, input_tag):
    """Check a Lyra input entry without comparing wrapped UObject identity."""
    expected_action_path = asset_path(action)
    return any(
        asset_path(entry.get_editor_property("input_action")) == expected_action_path
        and gameplay_tags_equal(entry.get_editor_property("input_tag"), input_tag)
        for entry in entries
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

input_action_specs = [
    ("IA_TopDownMoveForward", unreal.InputActionValueType.AXIS1D, "move_forward_input_tag", "W"),
    ("IA_TopDownMoveBackward", unreal.InputActionValueType.AXIS1D, "move_backward_input_tag", "S"),
    ("IA_TopDownMoveRight", unreal.InputActionValueType.AXIS1D, "move_right_input_tag", "D"),
    ("IA_TopDownMoveLeft", unreal.InputActionValueType.AXIS1D, "move_left_input_tag", "A"),
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
    # Held movement needs Triggered for the full key-down lifetime and Completed on release.
    # A stale Pressed trigger would complete one frame after activation and end the Ability.
    action.set_editor_property("triggers", [])
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

legacy_click_action = load_existing(f"{INPUT_ROOT}/IA_TopDownClick")
if legacy_click_action is not None:
    input_mapping.unmap_all_keys_from_action(legacy_click_action)

input_config_class = require(
    unreal.load_class(None, "/Script/LyraGame.LyraInputConfig"),
    "Failed to load ULyraInputConfig.",
)
input_config = require(
    load_or_create_data_asset(
        "DA_TopDown_InputConfig",
        INPUT_ROOT,
        input_config_class,
    ),
    "Failed to create the feature-owned Lyra InputConfig.",
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
# This config is injected as an additional Lyra config. It must contain only entries owned
# by TopDownFeature; duplicating DA_InputConfig_Base here would bind every base Ability twice.
native_actions = [
    unreal.LyraInputAction(input_action=action, input_tag=input_tag)
    for action, input_tag in zip(input_actions[4:], owned_tags[4:])
]
ability_actions = [
    unreal.LyraInputAction(input_action=action, input_tag=input_tag)
    for action, input_tag in zip(input_actions[:4], owned_tags[:4])
]
input_config.set_editor_property("native_input_actions", native_actions)
input_config.set_editor_property("ability_input_actions", ability_actions)

configured_native_actions = input_config.get_editor_property("native_input_actions")
configured_ability_actions = input_config.get_editor_property("ability_input_actions")
for action, input_tag in zip(input_actions[4:], owned_tags[4:]):
    require(
        has_input_action(configured_native_actions, action, input_tag),
        f"InputConfig did not retain native {action.get_name()} -> {input_tag}.",
    )
for action, input_tag in zip(input_actions[:4], owned_tags[:4]):
    require(
        has_input_action(configured_ability_actions, action, input_tag),
        f"InputConfig did not retain ability {action.get_name()} -> {input_tag}.",
    )

ability_set_class = require(
    unreal.load_class(None, "/Script/LyraGame.LyraAbilitySet"),
    "Failed to load ULyraAbilitySet.",
)
ability_set = require(
    load_or_create_data_asset(
        "DA_AbilitySet_TopDownMovement",
        ABILITY_ROOT,
        ability_set_class,
    ),
    "Failed to create the TopDown movement AbilitySet.",
)
move_ability_class = require(
    unreal.load_class(None, MOVE_ABILITY_CLASS_PATH),
    "Failed to load UTopDownGameplayAbility_Move; compile TopDownFeatureRuntime first.",
)
require(
    unreal.TopDownFeatureAssetLibrary.configure_ability_set_gameplay_abilities(
        ability_set,
        [move_ability_class] * 4,
        [1] * 4,
        owned_tags[:4],
    ),
    "Failed to configure DA_AbilitySet_TopDownMovement.",
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
pawn_data.set_editor_property("pawn_class", pawn_class)
# TopDownFeature's GameFeatureData grants the movement set and injects its additional input
# config. Keeping them out of PawnData prevents duplicate specs/bindings in consuming modes.
pawn_data.set_editor_property("ability_sets", [])
pawn_data.set_editor_property(
    "input_config",
    require(
        load_existing("/Game/Input/DA_InputConfig_Base"),
        "Missing /Game/Input/DA_InputConfig_Base.",
    ),
)
pawn_data.set_editor_property("default_camera_mode", camera_class)

lyra_character_class = require(
    unreal.load_class(None, "/Script/LyraGame.LyraCharacter"),
    "Failed to load ALyraCharacter.",
)
lyra_player_state_class = require(
    unreal.load_class(None, "/Script/LyraGame.LyraPlayerState"),
    "Failed to load ALyraPlayerState.",
)
actions = [
    action
    for action in game_feature_data.get_editor_property("actions")
    if action is not None and str(action.get_name()) not in OWNED_ACTION_CLASSES
]
actions.extend(
    [
        require(
            unreal.TopDownFeatureAssetLibrary.create_add_components_action(
                game_feature_data,
                lyra_character_class,
                component_class,
                unreal.Name("TopDown_AddComponents"),
            ),
            "Failed to create TopDown AddComponents action.",
        ),
        require(
            unreal.TopDownFeatureAssetLibrary.create_add_input_context_mapping_action(
                game_feature_data,
                input_mapping,
                1,
                unreal.Name("TopDown_AddInputMapping"),
            ),
            "Failed to create TopDown AddInputMapping action.",
        ),
        require(
            unreal.TopDownFeatureAssetLibrary.create_add_input_binding_action(
                game_feature_data,
                input_config,
                unreal.Name("TopDown_AddInputBinding"),
            ),
            "Failed to create TopDown AddInputBinding action.",
        ),
        require(
            unreal.TopDownFeatureAssetLibrary.create_add_abilities_action(
                game_feature_data,
                lyra_player_state_class,
                ability_set,
                unreal.Name("TopDown_AddAbilities"),
            ),
            "Failed to create TopDown AddAbilities action.",
        ),
    ]
)
game_feature_data.set_editor_property("actions", actions)
configured_actions = {
    str(action.get_name()): str(action.get_class().get_name())
    for action in game_feature_data.get_editor_property("actions")
    if action is not None
}
for action_name, expected_class in OWNED_ACTION_CLASSES.items():
    require(
        configured_actions.get(action_name) == expected_class,
        f"TopDownFeature did not retain {action_name} as {expected_class}.",
    )

assets_to_save = [
    game_feature_data,
    *input_actions,
    input_mapping,
    input_config,
    ability_set,
    pawn_data,
]
for asset in assets_to_save:
    save_asset(asset)

unreal.log_warning("TOPDOWN_ASSETS_CREATED")
unreal.log_warning("TopDown movement is GAS-owned; the PawnComponent now executes movement and camera presentation only.")
