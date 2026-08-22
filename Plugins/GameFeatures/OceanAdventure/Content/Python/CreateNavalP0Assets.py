"""Create the naval P0 gameplay layer: input, abilities, the beacon, and the P0 experience.

What this owns is everything that only means something once GAS and the match exist:

    IMC_OceanNaval / DA_InputConfig_OceanNaval   InputTag -> InputAction, no hard-coded keys
    DA_AbilitySet_OceanNaval                     the six naval abilities, granted by PawnData
    BP_Naval_GroundCannon                        the field emplacement of the shared cannon
    BP_NavalP0_Beacon                            the sudden-death beacon
    B_Experience_NavalP0                         the P0 match, with its own match component

The raft's own naval layer -- shootable build pieces and the vessel components on
ARaftActor -- is authored by the Raft feature's CreateRaftNavalAssets.py, because a
GameFeature may not reference another GameFeature's classes or assets.

Run after compiling NavalCoreRuntime and OceanAdventureRuntime, and after
CreateOceanAdventureExperience.py, which owns PawnData and the GameFeatureData. Safe to re-run.

    import CreateNavalP0Assets
    CreateNavalP0Assets.main()
"""

import unreal


FEATURE_ROOT = "/OceanAdventure"
INPUT_ROOT = f"{FEATURE_ROOT}/Input"
NAVAL_ROOT = f"{FEATURE_ROOT}/Naval"
EXPERIENCE_ROOT = f"{FEATURE_ROOT}/Experience"
GAME_FEATURE_DATA_PATH = f"{FEATURE_ROOT}/OceanAdventure"
PAWN_DATA_PATH = f"{FEATURE_ROOT}/Character/DA_OceanAdventure_PawnData"
EXPERIENCE_PATH = f"{EXPERIENCE_ROOT}/B_Experience_NavalP0"
BASE_EXPERIENCE_PATH = f"{EXPERIENCE_ROOT}/BP_Experience_Ocean"

INPUT_MAPPING_PATH = f"{INPUT_ROOT}/IMC_OceanNaval"
INPUT_CONFIG_PATH = f"{INPUT_ROOT}/DA_InputConfig_OceanNaval"
ABILITY_SET_PATH = f"{NAVAL_ROOT}/DA_AbilitySet_OceanNaval"
GROUND_CANNON_PATH = f"{NAVAL_ROOT}/BP_Naval_GroundCannon"
BEACON_PATH = f"{NAVAL_ROOT}/BP_NavalP0_Beacon"

GAME_FEATURES_TO_ENABLE = ["OceanAdventure", "TopDownFeature", "Raft"]
ACTION_SET_PATHS = [
    "/Game/ActionSet/LSA_Standard_Components",
    "/Game/ActionSet/LSA_Shared_Input",
    "/Game/ActionSet/LAS_Standard_HUD",
]

# One InputAction per key. Interact is deliberately one tag for two abilities: walking up to
# a wheel and walking up to a gun are the same player intent, and whichever ability finds
# nothing in range ends immediately.
ACTIONS = (
    ("IA_Naval_Interact", "InputTag.Naval.Interact", "E"),
    ("IA_Naval_Fire", "InputTag.Naval.Fire", "LeftMouseButton"),
    ("IA_Naval_Repair", "InputTag.Naval.Repair", "R"),
    ("IA_Naval_DeployHeavyWeapon", "InputTag.Naval.DeployHeavyWeapon", "C"),
    ("IA_Naval_DeployLifeRaft", "InputTag.Naval.DeployLifeRaft", "G"),
)

ABILITIES = (
    ("/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_OperateHelm", "InputTag.Naval.Interact"),
    ("/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_OperateHeavyWeapon", "InputTag.Naval.Interact"),
    ("/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_FireHeavyWeapon", "InputTag.Naval.Fire"),
    ("/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_EmergencyRepair", "InputTag.Naval.Repair"),
    ("/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_DeployHeavyWeapon", "InputTag.Naval.DeployHeavyWeapon"),
    ("/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_DeployLifeRaft", "InputTag.Naval.DeployLifeRaft"),
)

OWNED_ACTION_CLASSES = {
    "OceanNaval_AddInputMapping": "GameFeatureAction_AddInputContextMapping",
    "OceanNaval_AddInputBinding": "GameFeatureAction_AddInputBinding",
}
EXPERIENCE_ACTION_NAME = "NavalP0_AddMatchComponent"


def log(message):
    unreal.log(f"[NavalP0Assets] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def require_type(type_name, source):
    return require(
        getattr(unreal, type_name, None),
        f"unreal.{type_name} is missing. Compile {source} and restart the editor.",
    )


def gameplay_tag(tag_name):
    """Resolve a tag through the registry without constructing an unregistered loose tag."""
    request_tag = getattr(unreal.GameplayTagLibrary, "request_gameplay_tag", None)
    if request_tag is not None:
        tag = request_tag(unreal.Name(tag_name), False)
    else:
        tag = unreal.GameplayTag()
        tag.import_text(tag_name)

    if not unreal.GameplayTagLibrary.is_gameplay_tag_valid(tag):
        raise RuntimeError(
            f"GameplayTag '{tag_name}' is not registered. It is declared natively in "
            "OceanAdventureNavalTags.cpp, so compile OceanAdventureRuntime first."
        )
    return tag


def split_path(asset_path):
    package_path, _, asset_name = asset_path.rpartition("/")
    return package_path, asset_name


def save(asset):
    require(
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False),
        f"Unable to save {asset.get_path_name()}",
    )


def load_or_create(asset_path, asset_class, factory=None):
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return require(unreal.EditorAssetLibrary.load_asset(asset_path), f"Unable to load {asset_path}")

    package_path, asset_name = split_path(asset_path)
    if factory is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", asset_class)
    return require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, asset_class, factory
        ),
        f"Unable to create {asset_path}",
    )


def get_or_create_blueprint(asset_path, parent_class):
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return require(unreal.EditorAssetLibrary.load_asset(asset_path), f"Unable to load {asset_path}")

    package_path, asset_name = split_path(asset_path)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    return require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, unreal.Blueprint, factory
        ),
        f"Unable to create {asset_path}",
    )


def blueprint_class(blueprint, asset_path):
    generated = blueprint.generated_class()
    if generated is not None:
        return generated
    return require(
        unreal.EditorAssetLibrary.load_blueprint_class(asset_path),
        f"Unable to resolve the generated class of {asset_path}",
    )


def make_key(key_name):
    key = unreal.Key()
    key.set_editor_property("key_name", unreal.Name(key_name))
    return key


def configure_pressed_trigger(action):
    """Fire once per physical press, never once per held frame.

    Lyra binds ability input to Triggered, so without an edge trigger an instant ability
    would reactivate every frame the key is down.
    """
    pressed_class = require_type("InputTriggerPressed", "the Enhanced Input plugin")
    existing = [
        trigger
        for trigger in action.get_editor_property("triggers")
        if isinstance(trigger, pressed_class)
    ]
    pressed_trigger = existing[0] if existing else require(
        unreal.new_object(pressed_class, outer=action, name=unreal.Name("OceanNaval_Pressed")),
        f"Failed to create Pressed trigger for {action.get_path_name()}",
    )
    action.set_editor_property("triggers", [pressed_trigger])


def configure_input_assets():
    input_mapping = load_or_create(
        INPUT_MAPPING_PATH,
        unreal.InputMappingContext,
        unreal.InputMappingContext_Factory() if hasattr(unreal, "InputMappingContext_Factory") else None,
    )
    input_config = load_or_create(INPUT_CONFIG_PATH, unreal.LyraInputConfig)

    ability_actions = []
    for action_name, tag_name, key_name in ACTIONS:
        action_path = f"{INPUT_ROOT}/{action_name}"
        action = load_or_create(
            action_path,
            unreal.InputAction,
            unreal.InputActionFactory() if hasattr(unreal, "InputActionFactory") else None,
        )
        action.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
        configure_pressed_trigger(action)
        save(action)

        input_mapping.unmap_all_keys_from_action(action)
        input_mapping.map_key(action, make_key(key_name))
        ability_actions.append(
            unreal.LyraInputAction(input_action=action, input_tag=gameplay_tag(tag_name))
        )

    save(input_mapping)
    # Dispatched by ULyraHeroComponent through the InputTag, so no key is ever read directly.
    input_config.set_editor_property("ability_input_actions", ability_actions)
    save(input_config)
    return input_mapping, input_config


def configure_ability_set():
    ability_set = load_or_create(ABILITY_SET_PATH, unreal.LyraAbilitySet)
    classes = []
    levels = []
    tags = []
    for ability_path, tag_name in ABILITIES:
        classes.append(
            require(
                unreal.load_class(None, ability_path),
                f"Failed to load {ability_path}; compile OceanAdventureRuntime first",
            )
        )
        levels.append(1)
        tags.append(gameplay_tag(tag_name))

    require(
        unreal.OceanAdventureAssetLibrary.configure_ability_set_gameplay_abilities(
            ability_set, classes, levels, tags
        ),
        "Failed to configure DA_AbilitySet_OceanNaval gameplay abilities",
    )
    save(ability_set)
    return ability_set


def configure_placeable_blueprints():
    """The field cannon and the beacon, as Blueprints designers can drop into a level."""
    weapon_class = require(
        unreal.load_class(None, "/Script/NavalCoreRuntime.NavalHeavyWeaponActor"),
        "Failed to load ANavalHeavyWeaponActor; compile NavalCoreRuntime first",
    )
    cannon = get_or_create_blueprint(GROUND_CANNON_PATH, weapon_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(cannon)
    save(cannon)

    beacon_class = require(
        unreal.load_class(None, "/Script/OceanAdventureRuntime.OceanAdventureNavalBeaconActor"),
        "Failed to load AOceanAdventureNavalBeaconActor; compile OceanAdventureRuntime first",
    )
    beacon = get_or_create_blueprint(BEACON_PATH, beacon_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(beacon)
    save(beacon)
    return cannon, beacon


def configure_game_feature_data(input_mapping, input_config):
    game_feature_data = require(
        unreal.EditorAssetLibrary.load_asset(GAME_FEATURE_DATA_PATH),
        f"Missing {GAME_FEATURE_DATA_PATH}; run CreateGameFeatureData.py first",
    )
    # Named actions so a re-run replaces this script's own entries and leaves every other
    # action -- including hand-configured ones -- untouched.
    actions = [
        action
        for action in game_feature_data.get_editor_property("actions")
        if action is not None and str(action.get_name()) not in OWNED_ACTION_CLASSES
    ]
    actions.append(
        require(
            unreal.OceanAdventureAssetLibrary.create_add_input_context_mapping_action(
                game_feature_data, input_mapping, 1, unreal.Name("OceanNaval_AddInputMapping")
            ),
            "Failed to create the naval Add Input Mapping action",
        )
    )
    actions.append(
        require(
            unreal.OceanAdventureAssetLibrary.create_add_input_binding_action(
                game_feature_data, [input_config], unreal.Name("OceanNaval_AddInputBinding")
            ),
            "Failed to create the naval Add Input Binding action",
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


def configure_pawn_data(ability_set):
    pawn_data = unreal.EditorAssetLibrary.load_asset(PAWN_DATA_PATH)
    if pawn_data is None:
        log(f"{PAWN_DATA_PATH} is missing; run CreateOceanAdventureExperience.py first")
        return None

    ability_sets = list(pawn_data.get_editor_property("ability_sets"))
    if ability_set not in ability_sets:
        ability_sets.append(ability_set)
    pawn_data.set_editor_property("ability_sets", ability_sets)
    save(pawn_data)
    return pawn_data


def configure_experience(pawn_data):
    """The P0 match: the same pawn and features, plus the component that ends the match."""
    experience_class = require_type("LyraExperienceDefinition", "LyraGame")
    blueprint = get_or_create_blueprint(EXPERIENCE_PATH, experience_class)
    experience = unreal.get_default_object(blueprint_class(blueprint, EXPERIENCE_PATH))

    if pawn_data is not None:
        experience.set_editor_property("default_pawn_data", pawn_data)
    experience.set_editor_property("game_features_to_enable", GAME_FEATURES_TO_ENABLE)
    experience.set_editor_property(
        "action_sets",
        [
            require(unreal.EditorAssetLibrary.load_asset(path), f"Missing action set: {path}")
            for path in ACTION_SET_PATHS
        ],
    )

    # The match component belongs to this experience, not to the feature: the build sandbox
    # experience activates the same features and must not suddenly end on a timer.
    match_component_class = require_type("OceanAdventureNavalMatchComponent", "OceanAdventureRuntime")
    game_state_class = require(
        unreal.load_class(None, "/Script/LyraGame.LyraGameState"),
        "Failed to load ALyraGameState",
    )
    actions = [
        action
        for action in experience.get_editor_property("actions")
        if action is not None and str(action.get_name()) != EXPERIENCE_ACTION_NAME
    ]
    actions.append(
        require(
            unreal.NavalCoreAssetLibrary.create_add_components_action(
                experience,
                [game_state_class],
                [match_component_class],
                False,
                True,
                unreal.Name(EXPERIENCE_ACTION_NAME),
            ),
            "Failed to create the match component AddComponents action",
        )
    )
    experience.set_editor_property("actions", actions)

    # Blueprint defaults only persist through a compile, and compilation can replace the
    # generated class, so everything is re-read afterwards.
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    configured = unreal.get_default_object(blueprint_class(blueprint, EXPERIENCE_PATH))
    require(
        list(configured.get_editor_property("game_features_to_enable")) == GAME_FEATURES_TO_ENABLE,
        "Experience GameFeaturesToEnable did not persist after compilation",
    )
    require(
        any(
            action is not None and str(action.get_name()) == EXPERIENCE_ACTION_NAME
            for action in configured.get_editor_property("actions")
        ),
        "Experience did not retain the match component action",
    )
    save(blueprint)
    return blueprint


def main():
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([FEATURE_ROOT], True, True)

    input_mapping, input_config = configure_input_assets()
    ability_set = configure_ability_set()
    configure_placeable_blueprints()
    configure_game_feature_data(input_mapping, input_config)
    pawn_data = configure_pawn_data(ability_set)
    configure_experience(pawn_data)

    log(
        "Naval input, abilities, placeables and the P0 experience are configured. "
        "Restart the editor so the game features re-register, then run BuildNavalP0Map.py."
    )


if __name__ == "__main__":
    main()
