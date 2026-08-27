"""Create the naval P0 gameplay layer: input, abilities, the beacon, and the P0 experience.

What this owns is everything that only means something once GAS and the match exist:

    IMC_OceanNaval / DA_InputConfig_OceanNaval   InputTag -> InputAction, no hard-coded keys
    IMC_OceanHelm                                W/S -> signed throttle, A/D -> signed steer
    DA_AbilitySet_OceanNaval                     station/deploy abilities granted by PawnData
    BP_NavalP0_Beacon                            the sudden-death beacon
    B_Experience_NavalP0                         the P0 match, its match component, and the
                                                 reconnect anchor table + spawning manager

The cannon is not here. Ground emplacement and deck gun are one weapon under design 7.10, so
the Blueprint, its shell and their art live in the general framework plugin
(/NavalCore/Naval/BP_Naval_Cannon, authored by NavalCore's CreateNavalCoreCannon.py) where
both features are allowed to reach them. This feature only names it, through
OceanAdventureNavalSettings.GroundHeavyWeaponClass in Config/DefaultGame.ini.

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
BASE_INPUT_CONFIG_PATH = f"{INPUT_ROOT}/DA_InputConfig_OceanAdventure"
EXPERIENCE_PATH = f"{EXPERIENCE_ROOT}/B_Experience_NavalP0"
BASE_EXPERIENCE_PATH = f"{EXPERIENCE_ROOT}/BP_Experience_Ocean"

INPUT_MAPPING_PATH = f"{INPUT_ROOT}/IMC_OceanNaval"
INPUT_CONFIG_PATH = f"{INPUT_ROOT}/DA_InputConfig_OceanNaval"
HELM_MAPPING_PATH = f"{INPUT_ROOT}/IMC_OceanHelm"
ABILITY_SET_PATH = f"{NAVAL_ROOT}/DA_AbilitySet_OceanNaval"
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

HELM_ACTION_SPECS = (
    ("IA_Ocean_Helm_Throttle", "W/S"),
    ("IA_Ocean_Helm_Steer", "A/D"),
)


ABILITIES = (
    ("/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_OperateHelm", "InputTag.Naval.Interact"),
    ("/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_OperateHeavyWeapon", "InputTag.Naval.Interact"),
    # FireHeavyWeapon is intentionally absent: successful E interaction grants that spec
    # temporarily with the occupied HeavyWeapon Actor as its SourceObject.
    ("/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_EmergencyRepair", "InputTag.Naval.Repair"),
    ("/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_DeployHeavyWeapon", "InputTag.Naval.DeployHeavyWeapon"),
    ("/Script/OceanAdventureRuntime.OceanAdventureGameplayAbility_DeployLifeRaft", "InputTag.Naval.DeployLifeRaft"),
)

OWNED_ACTION_CLASSES = {
    "OceanNaval_AddInputMapping": "GameFeatureAction_AddInputContextMapping",
    "OceanNaval_AddInputBinding": "GameFeatureAction_AddInputBinding",
    "OceanNaval_AddHelmInputComponent": "GameFeatureAction_AddComponents",
}
EXPERIENCE_ACTION_NAME = "NavalP0_AddMatchComponent"
# The reconnect anchor table and the spawning manager that reads it. Server-only: nothing
# here is replicated, and the client learns where it came back by being put there.
RECONNECT_ACTION_NAME = "NavalP0_AddReconnectComponents"
RECONNECT_COMPONENTS = (
    "OceanAdventureNavalReconnectComponent",
    "OceanAdventureNavalSpawningComponent",
)


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


def gameplay_tags_equal(left, right):
    """Compare tag values, not UE Python wrapper identity."""
    equal_tag = getattr(unreal.GameplayTagLibrary, "equal_equal_gameplay_tag", None)
    if equal_tag is not None:
        return bool(equal_tag(left, right))
    export_left = getattr(left, "export_text", None)
    export_right = getattr(right, "export_text", None)
    if export_left is not None and export_right is not None:
        return str(export_left()) == str(export_right())
    return left == right


def split_path(asset_path):
    package_path, _, asset_name = asset_path.rpartition("/")
    return package_path, asset_name


def asset_path(asset):
    """Stable UObject package path comparison across UE Python wrapper instances."""
    if asset is None:
        return ""
    return str(asset.get_path_name()).split(".", 1)[0]


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
        if tag_name == "InputTag.Naval.Fire":
            # Fire remains Triggered for the full hold so GAS receives a real Completed
            # event on physical release; the active ability absorbs repeated press samples.
            action.set_editor_property("triggers", [])
            require(
                len(action.get_editor_property("triggers")) == 0,
                "IA_Naval_Fire still has an edge trigger; hold/release firing would complete immediately",
            )
        else:
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

    # The helm controls are continuous Axis1D actions, not ability activators. They are bound
    # by UOceanAdventureHelmInputComponent only while the helm ability owns IMC_OceanHelm.
    helm_actions = []
    for action_name, _ in HELM_ACTION_SPECS:
        action = load_or_create(
            f"{INPUT_ROOT}/{action_name}",
            unreal.InputAction,
            unreal.InputActionFactory() if hasattr(unreal, "InputActionFactory") else None,
        )
        action.set_editor_property("value_type", unreal.InputActionValueType.AXIS1D)
        action.set_editor_property("triggers", [])
        save(action)
        helm_actions.append(action)

    # Lyra's native input path owns the action lookup. The helm actions are not ability
    # activators, but they still live in PawnData's InputConfig and are selected by explicit
    # InputTags rather than by physical keys or asset paths at runtime.
    base_input_config = require(
        unreal.EditorAssetLibrary.load_asset(BASE_INPUT_CONFIG_PATH),
        f"Missing {BASE_INPUT_CONFIG_PATH}; run CreateOceanAdventureExperience.py first",
    )
    helm_input_tags = [
        gameplay_tag("InputTag.Naval.Helm.Throttle"),
        gameplay_tag("InputTag.Naval.Helm.Steer"),
    ]
    replaced_tags = list(helm_input_tags)
    native_actions = [
        entry
        for entry in base_input_config.get_editor_property("native_input_actions")
        if not any(
            gameplay_tags_equal(entry.get_editor_property("input_tag"), replaced_tag)
            for replaced_tag in replaced_tags
        )
    ]
    preserved_native_count = len(native_actions)
    for action, input_tag in zip(helm_actions, helm_input_tags):
        native_actions.append(unreal.LyraInputAction(input_action=action, input_tag=input_tag))
    base_input_config.set_editor_property("native_input_actions", native_actions)
    configured_native_actions = base_input_config.get_editor_property("native_input_actions")
    require(
        len(configured_native_actions) == preserved_native_count + len(helm_actions),
        "Base InputConfig native actions changed by an unexpected amount while adding helm actions",
    )
    for action, input_tag in zip(helm_actions, helm_input_tags):
        require(
            any(
                asset_path(entry.get_editor_property("input_action")) == asset_path(action)
                and gameplay_tags_equal(entry.get_editor_property("input_tag"), input_tag)
                for entry in configured_native_actions
            ),
            f"InputConfig did not retain {action.get_name()} -> {input_tag}",
        )
    save(base_input_config)

    helm_mapping = load_or_create(
        HELM_MAPPING_PATH,
        unreal.InputMappingContext,
        unreal.InputMappingContext_Factory()
        if hasattr(unreal, "InputMappingContext_Factory")
        else None,
    )
    require(
        unreal.OceanAdventureAssetLibrary.configure_helm_input_mapping(
            helm_mapping, helm_actions[0], helm_actions[1]
        ),
        "Failed to configure signed IMC_OceanHelm W/S/D/A mappings",
    )
    save(helm_mapping)
    log("Configured IMC_OceanHelm with signed W/S throttle and A/D steer actions")
    return input_mapping, input_config, helm_mapping


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


def configure_helm_input_component(helm_mapping):
    """Set/validate editor-session CDO defaults; the native class also has a path fallback."""
    component_class = require_type(
        "OceanAdventureHelmInputComponent", "the OceanAdventureRuntime module"
    )
    component_cdo = unreal.get_default_object(component_class)
    throttle_tag = gameplay_tag("InputTag.Naval.Helm.Throttle")
    steer_tag = gameplay_tag("InputTag.Naval.Helm.Steer")
    component_cdo.set_editor_property("throttle_input_tag", throttle_tag)
    component_cdo.set_editor_property("steer_input_tag", steer_tag)
    component_cdo.set_editor_property("helm_mapping_priority", 2)

    configured_throttle = component_cdo.get_editor_property("throttle_input_tag")
    configured_steer = component_cdo.get_editor_property("steer_input_tag")
    configured_priority = int(component_cdo.get_editor_property("helm_mapping_priority"))
    require(
        gameplay_tags_equal(configured_throttle, throttle_tag),
        "Helm input component did not retain InputTag.Naval.Helm.Throttle",
    )
    require(
        gameplay_tags_equal(configured_steer, steer_tag),
        "Helm input component did not retain InputTag.Naval.Helm.Steer",
    )
    require(
        asset_path(helm_mapping) == f"{FEATURE_ROOT}/Input/IMC_OceanHelm",
        "Helm mapping asset path changed; native component fallback would not resolve it",
    )
    require(configured_priority == 2, "Helm input component did not retain mapping priority 2")
    log("Configured OceanAdventureHelmInputComponent with IMC_OceanHelm and both actions")
    return component_class


def configure_beacon_blueprint():
    """The sudden-death beacon, as a Blueprint designers can drop into a level.

    The field cannon used to be authored here too. It is now the shared gun in
    /NavalCore/Naval/BP_Naval_Cannon: the deck gun and the emplacement are the same weapon, and
    only a general-framework asset can be named by both features without one of them reaching
    into the other. This feature points at it through
    OceanAdventureNavalSettings.GroundHeavyWeaponClass.
    """
    beacon_class = require(
        unreal.load_class(None, "/Script/OceanAdventureRuntime.OceanAdventureNavalBeaconActor"),
        "Failed to load AOceanAdventureNavalBeaconActor; compile OceanAdventureRuntime first",
    )
    beacon = get_or_create_blueprint(BEACON_PATH, beacon_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(beacon)
    save(beacon)
    return beacon


def configure_game_feature_data(input_mapping, input_config, helm_component_class):
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
    pawn_class = require(
        unreal.load_class(None, "/Script/OceanAdventureRuntime.OceanAdventurePawn"),
        "Failed to load AOceanAdventurePawn for helm input component injection",
    )
    actions.append(
        require(
            unreal.OceanAdventureAssetLibrary.create_add_components_action(
                game_feature_data,
                [pawn_class],
                [helm_component_class],
                True,
                True,
                unreal.Name("OceanNaval_AddHelmInputComponent"),
            ),
            "Failed to create the helm input component Add Components action",
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
    require(
        pawn_data is not None,
        f"{PAWN_DATA_PATH} is missing; run CreateOceanAdventureExperience.py first",
    )
    expected_input_config = require(
        unreal.EditorAssetLibrary.load_asset(BASE_INPUT_CONFIG_PATH),
        f"{BASE_INPUT_CONFIG_PATH} is missing; run CreateOceanAdventureExperience.py first",
    )
    actual_input_config = pawn_data.get_editor_property("input_config")
    require(
        actual_input_config == expected_input_config,
        (
            f"{PAWN_DATA_PATH} still uses {actual_input_config}; run "
            "CreateOceanAdventureExperience.py before CreateNavalP0Assets.py"
        ),
    )

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
    owned_action_names = {EXPERIENCE_ACTION_NAME, RECONNECT_ACTION_NAME}
    actions = [
        action
        for action in experience.get_editor_property("actions")
        if action is not None and str(action.get_name()) not in owned_action_names
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

    # A dropped player's seat is freed by the station itself (NavalCore), but "come back where
    # you were" needs somewhere that outlives the player state to remember the position, and
    # Lyra's spawning hook to use it. Both are game state components, injected here rather
    # than by the feature: the build sandbox experience shares the same features and has no
    # match to reconnect into.
    reconnect_component_classes = [
        require_type(name, "OceanAdventureRuntime") for name in RECONNECT_COMPONENTS
    ]
    actions.append(
        require(
            unreal.NavalCoreAssetLibrary.create_add_components_action(
                experience,
                [game_state_class] * len(reconnect_component_classes),
                reconnect_component_classes,
                False,
                True,
                unreal.Name(RECONNECT_ACTION_NAME),
            ),
            "Failed to create the reconnect AddComponents action",
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
    configured_action_names = {
        str(action.get_name())
        for action in configured.get_editor_property("actions")
        if action is not None
    }
    for action_name in (EXPERIENCE_ACTION_NAME, RECONNECT_ACTION_NAME):
        require(
            action_name in configured_action_names,
            f"Experience did not retain the {action_name} action",
        )
    save(blueprint)
    return blueprint


def main():
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([FEATURE_ROOT], True, True)

    input_mapping, input_config, helm_mapping = configure_input_assets()
    ability_set = configure_ability_set()
    helm_component_class = configure_helm_input_component(helm_mapping)
    configure_beacon_blueprint()
    configure_game_feature_data(input_mapping, input_config, helm_component_class)
    pawn_data = configure_pawn_data(ability_set)
    configure_experience(pawn_data)

    log(
        "Naval input, abilities, the beacon and the P0 experience are configured. Run "
        "NavalCore's CreateNavalCoreCannon.py for the shared gun, restart the editor so the "
        "game features re-register, then run BuildNavalP0Map.py."
    )


if __name__ == "__main__":
    main()
