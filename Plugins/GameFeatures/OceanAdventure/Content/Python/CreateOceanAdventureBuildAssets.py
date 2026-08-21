"""Create the OceanAdventure build gameplay layer's assets.

Enhanced Input actions + mapping context, a Lyra InputConfig that carries the build
InputTags, and the AbilitySet that grants the three build abilities. Run after compiling
OceanAdventureRuntime and BuildingCoreRuntime. Safe to re-run.
"""

import unreal


FEATURE_ROOT = "/OceanAdventure"
INPUT_ROOT = f"{FEATURE_ROOT}/Input"
BUILD_ROOT = f"{FEATURE_ROOT}/Build"
GAME_FEATURE_DATA_PATH = f"{FEATURE_ROOT}/OceanAdventure"
PAWN_DATA_PATH = f"{FEATURE_ROOT}/Character/DA_OceanAdventure_PawnData"

# Key bindings. Confirm intentionally shares the left mouse button with click-to-move:
# the placement ability only activates while Status.Build.Active is present.
ACTIONS = (
    ("IA_Build_Mode", "InputTag.Build.Mode", "B",
     "/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_BuildMode"),
    ("IA_Build_Confirm", "InputTag.Build.Confirm", "LeftMouseButton",
     "/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_PlacePiece"),
    ("IA_Build_Remove", "InputTag.Build.Remove", "X",
     "/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_RemovePiece"),
)


def gameplay_tag(tag_name):
    """FGameplayTag's TagName is not exposed to Python, so tags must be requested from the
    registry instead of constructed. An unregistered tag returns the empty tag."""
    tag = unreal.GameplayTagLibrary.request_gameplay_tag(unreal.Name(tag_name), False)
    if tag == unreal.GameplayTag():
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
    granted_abilities = []
    for action_name, tag_name, key_name, ability_path in ACTIONS:
        action = require(
            load_or_create(
                action_name, INPUT_ROOT, unreal.InputAction,
                unreal.InputActionFactory() if hasattr(unreal, "InputActionFactory") else None,
            ),
            f"Failed to create {action_name}",
        )
        action.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
        save(action)

        input_mapping.unmap_all_keys_from_action(action)
        input_mapping.map_key(action, make_key(key_name))

        input_tag = gameplay_tag(tag_name)
        ability_actions.append(
            unreal.LyraInputAction(input_action=action, input_tag=input_tag)
        )

        ability_class = require(
            unreal.load_class(None, ability_path),
            f"Failed to load {ability_path}; compile OceanAdventureRuntime first",
        )
        granted_abilities.append(
            unreal.LyraAbilitySet_GameplayAbility(
                ability=ability_class,
                ability_level=1,
                input_tag=input_tag,
            )
        )

    save(input_mapping)

    # Ability input actions are dispatched by ULyraHeroComponent through the InputTag,
    # so the abilities are reachable without a single hard-coded key check.
    input_config.set_editor_property("ability_input_actions", ability_actions)
    save(input_config)

    ability_set.set_editor_property("granted_gameplay_abilities", granted_abilities)
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
    owned_names = {
        "OceanBuild_AddInputMapping",
        "OceanBuild_AddInputBinding",
        "OceanBuild_AddPreviewComponent",
    }
    actions = [
        action
        for action in game_feature_data.get_editor_property("actions")
        if action is not None and str(action.get_name()) not in owned_names
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
    game_feature_data.set_editor_property("actions", actions)
    save(game_feature_data)

    log("Build input, ability set and GameFeature actions configured")


if __name__ == "__main__":
    main()
