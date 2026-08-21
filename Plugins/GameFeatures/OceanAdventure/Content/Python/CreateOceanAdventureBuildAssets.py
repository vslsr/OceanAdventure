"""Create the OceanAdventure build gameplay layer's assets.

Enhanced Input actions + mapping context, a Lyra InputConfig that carries the build
InputTags, and the AbilitySet that grants the three build abilities. Run after compiling
OceanAdventureRuntime and BuildingCoreRuntime, and after CreateOceanAdventureExperience.py
because that base-experience generator rewrites PawnData and GameFeatureData. Safe to re-run.
"""

import unreal


FEATURE_ROOT = "/OceanAdventure"
INPUT_ROOT = f"{FEATURE_ROOT}/Input"
BUILD_ROOT = f"{FEATURE_ROOT}/Build"
GAME_FEATURE_DATA_PATH = f"{FEATURE_ROOT}/OceanAdventure"
PAWN_DATA_PATH = f"{FEATURE_ROOT}/Character/DA_OceanAdventure_PawnData"

# Key bindings. Confirm intentionally shares the left mouse button with click-to-move:
# the placement ability only activates while Status.Build.Active is present. Every action is
# configured with InputTriggerPressed because Lyra binds ability input to Triggered; without
# an edge trigger, an instant ability reactivates once per frame while the key remains held.
ACTIONS = (
    ("IA_Build_Mode", "InputTag.Build.Mode", "B",
     "/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_BuildMode"),
    ("IA_Build_Confirm", "InputTag.Build.Confirm", "LeftMouseButton",
     "/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_PlacePiece"),
    ("IA_Build_Remove", "InputTag.Build.Remove", "X",
     "/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_RemovePiece"),
)

OWNED_ACTION_CLASSES = {
    "OceanBuild_AddInputMapping": "GameFeatureAction_AddInputContextMapping",
    "OceanBuild_AddInputBinding": "GameFeatureAction_AddInputBinding",
    "OceanBuild_AddPreviewComponent": "GameFeatureAction_AddComponents",
    "OceanBuild_AddProximityComponent": "GameFeatureAction_AddComponents",
}


def gameplay_tag(tag_name):
    """Resolve a tag through the registry without constructing an unregistered loose tag."""
    request_tag = getattr(unreal.GameplayTagLibrary, "request_gameplay_tag", None)
    if request_tag is not None:
        tag = request_tag(unreal.Name(tag_name), False)
    else:
        # UE 5.7 does not expose FGameplayTag::RequestGameplayTag to Python. Importing
        # the struct text still routes through GameplayTagsManager::ImportSingleGameplayTag,
        # so an unregistered name remains invalid instead of creating a loose tag.
        tag = unreal.GameplayTag()
        tag.import_text(tag_name)

    if not unreal.GameplayTagLibrary.is_gameplay_tag_valid(tag):
        raise RuntimeError(
            f"GameplayTag '{tag_name}' is not registered. Check the feature's Config/Tags ini, "
            "or compile the module that declares it natively."
        )
    return tag


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def log(message):
    unreal.log(f"[OceanAdventureBuildAssets] {message}")


def load_or_create(asset_name, package_path, asset_class, factory=None):
    asset_path = f"{package_path}/{asset_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return unreal.EditorAssetLibrary.load_asset(asset_path)

    if factory is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", asset_class)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, package_path, asset_class, factory
    )


def save(asset):
    require(
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False),
        f"Unable to save {asset.get_path_name()}",
    )


def make_key(key_name):
    key = unreal.Key()
    key.set_editor_property("key_name", unreal.Name(key_name))
    return key


def configure_pressed_trigger(action):
    """Make a Lyra ability action fire once per physical press, never once per held frame."""
    pressed_class = require(
        getattr(unreal, "InputTriggerPressed", None),
        "InputTriggerPressed is unavailable; verify the Enhanced Input plugin is enabled",
    )
    triggers = [
        trigger
        for trigger in action.get_editor_property("triggers")
        if trigger is not None
    ]
    pressed_trigger = next(
        (trigger for trigger in triggers if isinstance(trigger, pressed_class)),
        None,
    )
    if pressed_trigger is None:
        pressed_trigger = require(
            unreal.new_object(
                pressed_class,
                outer=action,
                name=unreal.Name("OceanBuild_Pressed"),
            ),
            f"Failed to create Pressed trigger for {action.get_path_name()}",
        )

    # These InputActions are wholly owned by this script. Keeping exactly one Pressed trigger
    # repairs legacy assets that had no trigger and avoids accumulating duplicate subobjects.
    action.set_editor_property("triggers", [pressed_trigger])
    configured_triggers = list(action.get_editor_property("triggers"))
    require(
        len(configured_triggers) == 1
        and isinstance(configured_triggers[0], pressed_class),
        f"{action.get_path_name()} did not retain its one-shot Pressed trigger",
    )


def validate_owned_actions(game_feature_data):
    actions_by_name = {
        str(action.get_name()): action
        for action in game_feature_data.get_editor_property("actions")
        if action is not None
    }
    missing = sorted(set(OWNED_ACTION_CLASSES) - set(actions_by_name))
    require(
        not missing,
        f"GameFeatureData did not retain build actions: {', '.join(missing)}",
    )

    for action_name, expected_class_name in OWNED_ACTION_CLASSES.items():
        actual_class_name = str(actions_by_name[action_name].get_class().get_name())
        require(
            actual_class_name == expected_class_name,
            (
                f"{action_name} has class {actual_class_name}, "
                f"expected {expected_class_name}"
            ),
        )

    log(f"Validated GameFeature actions: {', '.join(sorted(OWNED_ACTION_CLASSES))}")


def main():
    input_mapping = require(
        load_or_create(
            "IMC_OceanBuild", INPUT_ROOT, unreal.InputMappingContext,
            unreal.InputMappingContext_Factory()
            if hasattr(unreal, "InputMappingContext_Factory")
            else None,
        ),
        "Failed to create IMC_OceanBuild",
    )

    input_config = require(
        load_or_create("DA_InputConfig_OceanBuild", INPUT_ROOT, unreal.LyraInputConfig),
        "Failed to create DA_InputConfig_OceanBuild",
    )
    ability_set = require(
        load_or_create("DA_AbilitySet_OceanBuild", BUILD_ROOT, unreal.LyraAbilitySet),
        "Failed to create DA_AbilitySet_OceanBuild",
    )

    ability_actions = []
    granted_ability_classes = []
    granted_ability_levels = []
    granted_ability_tags = []
    for action_name, tag_name, key_name, ability_path in ACTIONS:
        action = require(
            load_or_create(
                action_name, INPUT_ROOT, unreal.InputAction,
                unreal.InputActionFactory() if hasattr(unreal, "InputActionFactory") else None,
            ),
            f"Failed to create {action_name}",
        )
        action.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
        configure_pressed_trigger(action)
        save(action)

        input_mapping.unmap_all_keys_from_action(action)
        input_mapping.map_key(action, make_key(key_name))

        input_tag = gameplay_tag(tag_name)
        # Unlike FLyraAbilitySet_GameplayAbility, this reflected struct supports keyword
        # construction in UE 5.7. Its EditDefaultsOnly fields reject instance mutation.
        ability_actions.append(
            unreal.LyraInputAction(input_action=action, input_tag=input_tag)
        )

        ability_class = require(
            unreal.load_class(None, ability_path),
            f"Failed to load {ability_path}; compile OceanAdventureRuntime first",
        )
        granted_ability_classes.append(ability_class)
        granted_ability_levels.append(1)
        granted_ability_tags.append(input_tag)

    log(
        "Configured one-shot Pressed triggers: "
        + ", ".join(action_name for action_name, _, _, _ in ACTIONS)
    )
    save(input_mapping)

    # Ability input actions are dispatched by ULyraHeroComponent through the InputTag,
    # so the abilities are reachable without a single hard-coded key check.
    input_config.set_editor_property("ability_input_actions", ability_actions)
    save(input_config)

    require(
        unreal.OceanAdventureAssetLibrary.configure_ability_set_gameplay_abilities(
            ability_set,
            granted_ability_classes,
            granted_ability_levels,
            granted_ability_tags,
        ),
        "Failed to configure DA_AbilitySet_OceanBuild gameplay abilities",
    )
    save(ability_set)

    pawn_data = unreal.EditorAssetLibrary.load_asset(PAWN_DATA_PATH)
    if pawn_data is not None:
        ability_sets = list(pawn_data.get_editor_property("ability_sets"))
        if ability_set not in ability_sets:
            ability_sets.append(ability_set)
        pawn_data.set_editor_property("ability_sets", ability_sets)
        save(pawn_data)

    game_feature_data = require(
        unreal.EditorAssetLibrary.load_asset(GAME_FEATURE_DATA_PATH),
        f"Missing {GAME_FEATURE_DATA_PATH}; run CreateGameFeatureData.py first",
    )
    # Actions this script owns are named, so a re-run replaces them instead of appending a
    # duplicate, and every action configured by hand in the editor is left untouched.
    actions = [
        action
        for action in game_feature_data.get_editor_property("actions")
        if action is not None and str(action.get_name()) not in OWNED_ACTION_CLASSES
    ]
    actions.append(
        require(
            unreal.OceanAdventureAssetLibrary.create_add_input_context_mapping_action(
                game_feature_data, input_mapping, 1, unreal.Name("OceanBuild_AddInputMapping")
            ),
            "Failed to create the Add Input Mapping action",
        )
    )
    actions.append(
        require(
            unreal.OceanAdventureAssetLibrary.create_add_input_binding_action(
                game_feature_data, [input_config], unreal.Name("OceanBuild_AddInputBinding")
            ),
            "Failed to create the Add Input Binding action",
        )
    )

    # The ghost lives on the player pawn, so the gameplay feature that owns the player
    # injects it -- the Raft feature has no business knowing about the pawn.
    preview_class = require(
        getattr(unreal, "BuildPreviewComponent", None),
        "BuildPreviewComponent is unavailable; compile BuildingCoreRuntime first",
    )
    pawn_class = require(
        unreal.load_class(None, "/Script/OceanAdventureRuntime.OceanAdventurePawn"),
        "Failed to load AOceanAdventurePawn",
    )
    actions.append(
        require(
            unreal.OceanAdventureAssetLibrary.create_add_components_action(
                game_feature_data,
                [pawn_class],
                [preview_class],
                True,
                True,
                unreal.Name("OceanBuild_AddPreviewComponent"),
            ),
            "Failed to create the build preview AddComponents action",
        )
    )

    # Keeps Status.Build.HostAvailable up to date so the build key is inert away from a raft.
    proximity_class = require(
        getattr(unreal, "OceanAdventureBuildProximityComponent", None),
        "OceanAdventureBuildProximityComponent is unavailable; compile OceanAdventureRuntime first",
    )
    actions.append(
        require(
            unreal.OceanAdventureAssetLibrary.create_add_components_action(
                game_feature_data,
                [pawn_class],
                [proximity_class],
                True,
                True,
                unreal.Name("OceanBuild_AddProximityComponent"),
            ),
            "Failed to create the build proximity AddComponents action",
        )
    )
    game_feature_data.set_editor_property("actions", actions)
    save(game_feature_data)
    validate_owned_actions(game_feature_data)

    log("Build input, ability set and GameFeature actions configured; restart the editor before PIE")


if __name__ == "__main__":
    main()
