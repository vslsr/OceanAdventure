"""Create the carry gameplay layer: the F key, the ability, and the two injected components.

    IMC_OceanCarry / DA_InputConfig_OceanCarry   InputTag.Carry -> IA_Ocean_Carry (F)
    DA_AbilitySet_OceanCarry                     the toggle ability, granted by PawnData
    OceanCarry_AddComponents                     UCarrierComponent  -> AOceanAdventurePawn
                                                 UCarryableComponent -> ANavalHeavyWeaponActor
                                                 UCarryableComponent -> ANavalHelmActor

Nothing here is authored onto the cannon Blueprint: an experience that does not enable this
feature gets a gun nobody can lift, which is the point of injecting the component instead of
building it into the Actor.

Run after compiling CarryCoreRuntime, NavalCoreRuntime and OceanAdventureRuntime, and after
CreateOceanAdventureExperience.py, which owns PawnData and the GameFeatureData. Safe to
re-run: it replaces its own named actions and leaves every other action alone.

    import CreateOceanAdventureCarryAssets
    CreateOceanAdventureCarryAssets.main()
"""

import unreal

# The naval script owns the shared authoring helpers (registry-safe tag lookup, idempotent
# asset creation, the edge trigger). Same feature, same folder, one copy of each.
from CreateNavalP0Assets import (
    asset_path,
    configure_pressed_trigger,
    gameplay_tag,
    load_or_create,
    make_key,
    require,
    require_type,
    save,
)


FEATURE_ROOT = "/OceanAdventure"
INPUT_ROOT = f"{FEATURE_ROOT}/Input"
CARRY_ROOT = f"{FEATURE_ROOT}/Carry"
GAME_FEATURE_DATA_PATH = f"{FEATURE_ROOT}/OceanAdventure"
PAWN_DATA_PATH = f"{FEATURE_ROOT}/Character/DA_OceanAdventure_PawnData"

INPUT_ACTION_PATH = f"{INPUT_ROOT}/IA_Ocean_Carry"
INPUT_MAPPING_PATH = f"{INPUT_ROOT}/IMC_OceanCarry"
INPUT_CONFIG_PATH = f"{INPUT_ROOT}/DA_InputConfig_OceanCarry"
ABILITY_SET_PATH = f"{CARRY_ROOT}/DA_AbilitySet_OceanCarry"

CARRY_INPUT_TAG = "InputTag.Carry"
# F, not B: B is already IA_Build_Mode. Two abilities on one InputTag would both
# activate on the same press, in whatever order their specs happen to sit in.
CARRY_KEY = "F"
CARRY_ABILITY_PATH = "/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_Carry"

# Named so a re-run replaces this script's own entries and leaves the naval ones, and any
# hand-configured action, untouched.
OWNED_ACTION_CLASSES = {
    "OceanCarry_AddInputMapping": "GameFeatureAction_AddInputContextMapping",
    "OceanCarry_AddInputBinding": "GameFeatureAction_AddInputBinding",
    "OceanCarry_AddComponents": "GameFeatureAction_AddComponents",
}

# Pairs, in order: which component goes on which Actor class.
INJECTED_COMPONENTS = (
    ("/Script/OceanAdventureRuntime.OceanAdventurePawn", "CarrierComponent"),
    ("/Script/NavalCoreRuntime.NavalHeavyWeaponActor", "CarryableComponent"),
    # The helm follows the same rule as the gun: built onto a deck, and liftable off it again.
    # Picking one up attaches it to the character, which is exactly what makes the vessel stop
    # recognising it as a way to steer -- no extra bookkeeping is needed for that.
    ("/Script/NavalCoreRuntime.NavalHelmActor", "CarryableComponent"),
)


def log(message):
    unreal.log(f"[OceanCarryAssets] {message}")


def configure_input_assets():
    action = load_or_create(
        INPUT_ACTION_PATH,
        unreal.InputAction,
        unreal.InputActionFactory() if hasattr(unreal, "InputActionFactory") else None,
    )
    action.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
    # One activation per physical press: the ability toggles, so a per-frame retrigger would
    # pick the gun up and put it down again every tick the key is held.
    configure_pressed_trigger(action)
    save(action)

    input_mapping = load_or_create(
        INPUT_MAPPING_PATH,
        unreal.InputMappingContext,
        unreal.InputMappingContext_Factory() if hasattr(unreal, "InputMappingContext_Factory") else None,
    )
    input_mapping.unmap_all_keys_from_action(action)
    input_mapping.map_key(action, make_key(CARRY_KEY))
    save(input_mapping)

    input_config = load_or_create(INPUT_CONFIG_PATH, unreal.LyraInputConfig)
    input_config.set_editor_property(
        "ability_input_actions",
        [unreal.LyraInputAction(input_action=action, input_tag=gameplay_tag(CARRY_INPUT_TAG))],
    )
    save(input_config)

    configured = input_config.get_editor_property("ability_input_actions")
    require(
        len(configured) == 1 and asset_path(configured[0].get_editor_property("input_action")) == INPUT_ACTION_PATH,
        "DA_InputConfig_OceanCarry did not retain IA_Ocean_Carry",
    )
    log(f"Mapped {CARRY_KEY} -> {CARRY_INPUT_TAG} through IMC_OceanCarry")
    return input_mapping, input_config


def configure_ability_set():
    ability_set = load_or_create(ABILITY_SET_PATH, unreal.LyraAbilitySet)
    ability_class = require(
        unreal.load_class(None, CARRY_ABILITY_PATH),
        f"Failed to load {CARRY_ABILITY_PATH}; compile OceanAdventureRuntime first",
    )
    require(
        unreal.OceanAdventureAssetLibrary.configure_ability_set_gameplay_abilities(
            ability_set, [ability_class], [1], [gameplay_tag(CARRY_INPUT_TAG)]
        ),
        "Failed to configure DA_AbilitySet_OceanCarry gameplay abilities",
    )
    save(ability_set)
    return ability_set


def configure_game_feature_data(input_mapping, input_config):
    game_feature_data = require(
        unreal.EditorAssetLibrary.load_asset(GAME_FEATURE_DATA_PATH),
        f"Missing {GAME_FEATURE_DATA_PATH}; run CreateGameFeatureData.py first",
    )
    actions = [
        action
        for action in game_feature_data.get_editor_property("actions")
        if action is not None and str(action.get_name()) not in OWNED_ACTION_CLASSES
    ]
    actions.append(
        require(
            unreal.OceanAdventureAssetLibrary.create_add_input_context_mapping_action(
                game_feature_data, input_mapping, 1, unreal.Name("OceanCarry_AddInputMapping")
            ),
            "Failed to create the carry Add Input Mapping action",
        )
    )
    actions.append(
        require(
            unreal.OceanAdventureAssetLibrary.create_add_input_binding_action(
                game_feature_data, [input_config], unreal.Name("OceanCarry_AddInputBinding")
            ),
            "Failed to create the carry Add Input Binding action",
        )
    )

    actor_classes = []
    component_classes = []
    for actor_class_path, component_type_name in INJECTED_COMPONENTS:
        actor_classes.append(
            require(
                unreal.load_class(None, actor_class_path),
                f"Failed to load {actor_class_path}; compile its module first",
            )
        )
        component_classes.append(require_type(component_type_name, "CarryCoreRuntime"))

    # Both flags on: the carryable replicates who is holding it, so the component has to
    # exist on the server and on every client for that state to land anywhere.
    actions.append(
        require(
            unreal.OceanAdventureAssetLibrary.create_add_components_action(
                game_feature_data,
                actor_classes,
                component_classes,
                True,
                True,
                unreal.Name("OceanCarry_AddComponents"),
            ),
            "Failed to create the carry Add Components action",
        )
    )

    game_feature_data.set_editor_property("actions", actions)
    save(game_feature_data)

    names = {
        str(action.get_name()): str(action.get_class().get_name())
        for action in game_feature_data.get_editor_property("actions")
        if action is not None
    }
    for action_name, expected_class in OWNED_ACTION_CLASSES.items():
        require(
            names.get(action_name) == expected_class,
            f"GameFeatureData did not retain {action_name} as {expected_class}",
        )
    log("GameFeatureData carries the carry input mapping, input binding and component actions")


def configure_pawn_data(ability_set):
    pawn_data = require(
        unreal.EditorAssetLibrary.load_asset(PAWN_DATA_PATH),
        f"{PAWN_DATA_PATH} is missing; run CreateOceanAdventureExperience.py first",
    )
    ability_sets = list(pawn_data.get_editor_property("ability_sets"))
    if ability_set not in ability_sets:
        ability_sets.append(ability_set)
    pawn_data.set_editor_property("ability_sets", ability_sets)
    save(pawn_data)

    require(
        any(asset_path(entry) == ABILITY_SET_PATH for entry in pawn_data.get_editor_property("ability_sets")),
        "PawnData did not retain DA_AbilitySet_OceanCarry",
    )
    return pawn_data


def main():
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([FEATURE_ROOT], True, True)

    input_mapping, input_config = configure_input_assets()
    ability_set = configure_ability_set()
    configure_game_feature_data(input_mapping, input_config)
    configure_pawn_data(ability_set)

    log(
        "Carry input, ability and component injection are configured. Restart the editor so "
        "the game feature re-registers, then press F next to a deployed field gun."
    )


if __name__ == "__main__":
    main()
