"""Create the Ocean Adventure pawn assets and wire them into the experience.

The pawn class itself carries no gameplay capability. This script builds the data
that turns it into the mode's player, and the wiring that injects everything else:

    BP_Experience_Ocean
      DefaultPawnData ------> DA_OceanAdventure_PawnData
                                PawnClass        -> BP_OceanAdventure_Pawn
                                AbilitySets      -> preserved build/naval grants
                                InputConfig      -> DA_InputConfig_OceanAdventure
                                DefaultCameraMode-> ULyraCameraMode_TopDownFollow
      GameFeaturesToEnable -> "OceanAdventure", "TopDownFeature", "Raft"

    OceanAdventure (GameFeatureData)
      AddComponents        -> LyraGameState        gets UOceanWorldManagerComponent
                              AOceanAdventurePawn  gets ULyraHeroComponent
                              AOceanAdventurePawn  gets UOceanChunkInvokerComponent
                              AOceanChunkActor     gets UOceanChunkPresentationComponent

    TopDownFeature (GameFeatureData, already configured)
      AddComponents        -> UTopDownPawnComponent

    OceanAdventure (GameFeatureData)
      AddInputMapping      -> IMC_OceanAdventure_Base (owned WASD/camera actions)

The chunk components live in OceanCore, a plain plugin. GameFeatureAction_AddComponents
takes any TSubclassOf<UActorComponent>, so OceanCore does not have to be a game feature
to be injected -- OceanAdventure activates and this plugin's action does the injecting.

The top down camera is not a component: Lyra reads it from PawnData.DefaultCameraMode.
That is why this experience needs its own PawnData instead of reusing
/TopDownFeature/Pawn/DA_TopDown_PawnData, which borrows SimpleExperience's pawn.

Run from the editor Python console. Unreal puts this plugin's Content/Python folder on
sys.path, so import by module name:

    import CreateOceanAdventureExperience

To run again in the same editor session:

    import importlib, CreateOceanAdventureExperience
    importlib.reload(CreateOceanAdventureExperience)

Requires the OceanAdventureRuntime module to be compiled, and CreateGameFeatureData to
have been run first so /OceanAdventure/OceanAdventure exists.

Idempotent: assets are created if missing, and owned properties are repaired on re-run.
Named base GameFeature actions are replaced while unrelated actions and PawnData AbilitySets
are preserved. Pawn movement tuning is stored on BP_OceanAdventure_Pawn, not its C++ base.
"""

import unreal


PAWN_BLUEPRINT_PATH = "/OceanAdventure/Character/BP_OceanAdventure_Pawn"
PAWN_DATA_PATH = "/OceanAdventure/Character/DA_OceanAdventure_PawnData"
GAME_FEATURE_DATA_PATH = "/OceanAdventure/OceanAdventure"
EXPERIENCE_PATH = "/OceanAdventure/Experience/BP_Experience_Ocean"

INPUT_ROOT = "/OceanAdventure/Input"
BASE_INPUT_CONFIG_PATH = "/Game/Input/DA_InputConfig_Base"
INPUT_CONFIG_PATH = f"{INPUT_ROOT}/DA_InputConfig_OceanAdventure"
INPUT_MAPPING_PATH = f"{INPUT_ROOT}/IMC_OceanAdventure_Base"
MAX_SWIM_SPEED = 600.0

TOP_DOWN_INPUT_SPECS = [
    ("IA_OceanAdventure_MoveForward", unreal.InputActionValueType.AXIS1D, "move_forward_input_tag", "W"),
    ("IA_OceanAdventure_MoveBackward", unreal.InputActionValueType.AXIS1D, "move_backward_input_tag", "S"),
    ("IA_OceanAdventure_MoveRight", unreal.InputActionValueType.AXIS1D, "move_right_input_tag", "D"),
    ("IA_OceanAdventure_MoveLeft", unreal.InputActionValueType.AXIS1D, "move_left_input_tag", "A"),
    ("IA_OceanAdventure_TopDownCameraZoom", unreal.InputActionValueType.AXIS1D, "camera_zoom_input_tag", "MouseWheelAxis"),
    ("IA_OceanAdventure_TopDownCameraRotateHold", unreal.InputActionValueType.BOOLEAN, "camera_rotate_hold_input_tag", "RightMouseButton"),
    ("IA_OceanAdventure_TopDownCameraRotate", unreal.InputActionValueType.AXIS2D, "camera_rotate_input_tag", "Mouse2D"),
]

BASE_COMPONENTS_ACTION_NAME = "OceanBase_AddComponents"
BASE_INPUT_MAPPING_ACTION_NAME = "OceanBase_AddInputMapping"

# LSA_Shared_Input adds /Game/Input/IMC_Base at priority 0, and IMC_Base maps W/A/S/D to
# IA_Move and Mouse2D to IA_Look_Mouse. At equal priority IMC_Base wins the key, so the
# mode's own move and camera-rotate actions never fire -- `showdebug enhancedinput` reports
# them as "OVERRIDDEN BY IMC_Base:IA_Move". Sit above it, where IMC_OceanBuild and
# IMC_OceanNaval already sit; none of those three map the keys this one claims.
BASE_INPUT_MAPPING_PRIORITY = 1

ACTION_SET_PATHS = [
    "/Game/ActionSet/LSA_Standard_Components",
    "/Game/ActionSet/LSA_Shared_Input",
    "/Game/ActionSet/LAS_Standard_HUD",
]

# Plugin names, not paths: Lyra resolves these through GetPluginURLByName.
# OceanAdventure enables itself so its own AddComponents action runs, the same way
# Lyra's ShooterCore experiences do.
GAME_FEATURES_TO_ENABLE = ["OceanAdventure", "TopDownFeature", "Raft"]


def log(message):
    unreal.log(f"[OceanAdventureExperience] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def require_type(type_name, source):
    found = getattr(unreal, type_name, None)
    if found is None:
        raise RuntimeError(
            f"unreal.{type_name} is missing. Compile {source} and restart the editor."
        )
    return found


def load_existing(asset_path):
    # GameFeature packages can exist on disk before the editor's cached AssetRegistry
    # notices them. Scan the owning package path before loading to avoid creating a
    # duplicate asset on the first run after a plugin checkout or migration.
    package_path, _, _ = asset_path.rpartition("/")
    if package_path:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        registry.scan_paths_synchronous([package_path], True, True)
    if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return None
    return unreal.EditorAssetLibrary.load_asset(asset_path)


def split_path(asset_path):
    package_path, _, asset_name = asset_path.rpartition("/")
    return package_path, asset_name


def save(asset_path):
    require(
        unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False),
        f"Failed to save: {asset_path}",
    )


def get_or_create_blueprint(asset_path, parent_class):
    existing = load_existing(asset_path)
    if existing is not None:
        log(f"Blueprint already exists: {asset_path}")
        return existing

    package_path, asset_name = split_path(asset_path)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)

    blueprint = require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, unreal.Blueprint, factory
        ),
        f"Failed to create blueprint: {asset_path}",
    )
    log(f"Created blueprint: {asset_path}")
    return blueprint


def get_or_create_data_asset(asset_path, asset_class, factory=None):
    existing = load_existing(asset_path)
    if existing is not None:
        log(f"Data asset already exists: {asset_path}")
        return existing

    package_path, asset_name = split_path(asset_path)
    if factory is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", asset_class)

    asset = require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, asset_class, factory
        ),
        f"Failed to create data asset: {asset_path}",
    )
    log(f"Created data asset: {asset_path}")
    return asset


def load_or_duplicate(source_path, destination_path):
    existing = load_existing(destination_path)
    if existing is not None:
        return existing

    return require(
        unreal.EditorAssetLibrary.duplicate_asset(source_path, destination_path),
        f"Failed to duplicate {source_path} to {destination_path}",
    )


def make_key(key_name):
    key = unreal.Key()
    key.set_editor_property("key_name", unreal.Name(key_name))
    return key


def gameplay_tag(tag_name):
    """Resolve a registered tag across UE 5.7 Python wrapper variants."""
    request_tag = getattr(unreal.GameplayTagLibrary, "request_gameplay_tag", None)
    if request_tag is not None:
        tag = request_tag(unreal.Name(tag_name), False)
    else:
        # Some UE 5.7 editor builds do not expose RequestGameplayTag to Python. Importing
        # the struct text still routes through GameplayTagsManager and preserves invalid tags.
        tag = unreal.GameplayTag()
        tag.import_text(tag_name)

    is_valid = getattr(unreal.GameplayTagLibrary, "is_gameplay_tag_valid", None)
    if tag == unreal.GameplayTag() or (is_valid is not None and not is_valid(tag)):
        raise RuntimeError(f"GameplayTag is not registered: {tag_name}")
    return tag


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


def has_native_input_action(entries, action, input_tag):
    """Check a Lyra input entry without comparing wrapped UObject identity."""
    expected_action_path = asset_path(action)
    return any(
        asset_path(entry.get_editor_property("input_action")) == expected_action_path
        and gameplay_tags_equal(entry.get_editor_property("input_tag"), input_tag)
        for entry in entries
    )


def configure_input_assets():
    """Own the mouse-facing WASD and camera actions used by this feature's PawnData."""
    input_config = load_or_duplicate(BASE_INPUT_CONFIG_PATH, INPUT_CONFIG_PATH)
    input_actions = []
    for asset_name, value_type, _, _ in TOP_DOWN_INPUT_SPECS:
        action = get_or_create_data_asset(
            f"{INPUT_ROOT}/{asset_name}",
            unreal.InputAction,
            unreal.InputActionFactory() if hasattr(unreal, "InputActionFactory") else None,
        )
        action.set_editor_property("value_type", value_type)
        input_actions.append(action)

    input_mapping = get_or_create_data_asset(
        INPUT_MAPPING_PATH,
        unreal.InputMappingContext,
        unreal.InputMappingContext_Factory()
        if hasattr(unreal, "InputMappingContext_Factory")
        else None,
    )
    for action, (_, _, _, key_name) in zip(input_actions, TOP_DOWN_INPUT_SPECS):
        input_mapping.unmap_all_keys_from_action(action)
        input_mapping.map_key(action, make_key(key_name))

    # Remove the old click mapping if this script is upgrading an existing asset. The
    # asset remains on disk for reference, but no click action is registered or mapped.
    legacy_click_action = load_existing(f"{INPUT_ROOT}/IA_OceanAdventure_TopDownClick")
    if legacy_click_action is not None:
        input_mapping.unmap_all_keys_from_action(legacy_click_action)

    component_class = require_type("TopDownPawnComponent", "the TopDownFeature plugin")
    component_cdo = unreal.get_default_object(component_class)
    owned_tags = [
        require(
            component_cdo.get_editor_property(tag_property),
            f"UTopDownPawnComponent has no valid {tag_property}",
        )
        for _, _, tag_property, _ in TOP_DOWN_INPUT_SPECS
    ]
    legacy_tags = [gameplay_tag("InputTag.TopDownClick"), gameplay_tag("InputTag.Move")]
    # Every tag this script is responsible for: the ones it re-adds below, plus the
    # superseded ones it must strip. ``in`` would compare the wrapped structs by
    # identity, which silently keeps InputTag.Move alive alongside the top-down
    # actions -- and then both ULyraHeroComponent::Input_Move and
    # UTopDownPawnComponent feed AddMovementInput on the same key press.
    replaced_tags = list(owned_tags) + legacy_tags
    native_actions = [
        action
        for action in input_config.get_editor_property("native_input_actions")
        if not any(
            gameplay_tags_equal(action.get_editor_property("input_tag"), replaced_tag)
            for replaced_tag in replaced_tags
        )
    ]
    for action, input_tag in zip(input_actions, owned_tags):
        native_actions.append(
            unreal.LyraInputAction(input_action=action, input_tag=input_tag)
        )
    input_config.set_editor_property("native_input_actions", native_actions)

    configured_native_actions = input_config.get_editor_property("native_input_actions")
    for action, input_tag in zip(input_actions, owned_tags):
        if not has_native_input_action(configured_native_actions, action, input_tag):
            raise RuntimeError(
                f"InputConfig did not retain {action.get_name()} -> {input_tag}"
            )

    for asset_name, _, _, _ in TOP_DOWN_INPUT_SPECS:
        save(f"{INPUT_ROOT}/{asset_name}")
    save(INPUT_MAPPING_PATH)
    save(INPUT_CONFIG_PATH)
    log("Configured OceanAdventure-owned mouse-facing WASD and camera actions, mapping and InputConfig")
    return input_config, input_mapping


def get_blueprint_class(blueprint, asset_path):
    generated = blueprint.generated_class()
    if generated is not None:
        return generated
    return require(
        unreal.EditorAssetLibrary.load_blueprint_class(asset_path),
        f"Unable to resolve the generated class of {asset_path}",
    )


def configure_pawn_blueprint(pawn_blueprint_class):
    pawn_defaults = unreal.get_default_object(pawn_blueprint_class)
    movement_component_class = require_type(
        "CharacterMovementComponent", "the Engine module"
    )
    movement_component = require(
        pawn_defaults.get_component_by_class(movement_component_class),
        f"{PAWN_BLUEPRINT_PATH} has no CharacterMovement component",
    )
    movement_component.set_editor_property("max_swim_speed", MAX_SWIM_SPEED)

    configured_speed = movement_component.get_editor_property("max_swim_speed")
    if abs(float(configured_speed) - MAX_SWIM_SPEED) > 0.01:
        raise RuntimeError(
            f"Failed to set {PAWN_BLUEPRINT_PATH} MaxSwimSpeed: {configured_speed}"
        )

    log(f"Pawn blueprint MaxSwimSpeed is {configured_speed}")


def make_add_components_action(outer, component_specs, action_name):
    asset_library = require_type(
        "OceanAdventureAssetLibrary", "the OceanAdventureRuntime module"
    )
    actor_classes = [actor_class for actor_class, _ in component_specs]
    component_classes = [component_class for _, component_class in component_specs]

    # FGameFeatureComponentEntry and the AddComponents subclass properties are not
    # exposed to UE 5.7 Python. The native bridge creates and fills the action while
    # Python retains responsibility for asset composition and saving.
    return require(
        asset_library.create_add_components_action(
            outer,
            actor_classes,
            component_classes,
            True,
            True,
            unreal.Name(action_name),
        ),
        "Failed to create the AddComponents action",
    )


def create_pawn_data(pawn_blueprint_class, input_config):
    pawn_data = get_or_create_data_asset(PAWN_DATA_PATH, unreal.LyraPawnData)

    pawn_data.set_editor_property("pawn_class", pawn_blueprint_class)
    # Build and naval scripts append their AbilitySets here. A base-experience repair must
    # never erase those grants.
    pawn_data.set_editor_property("input_config", input_config)

    # Comes from TopDownFeature, which this plugin already depends on in its .uplugin.
    # It is a native class, not plugin content, so it is not a cross-feature asset reference.
    camera_mode = require_type("LyraCameraMode_TopDownFollow", "the TopDownFeature plugin")
    pawn_data.set_editor_property("default_camera_mode", camera_mode)

    save(PAWN_DATA_PATH)
    configured_input = pawn_data.get_editor_property("input_config")
    require(
        configured_input == input_config,
        (
            f"{PAWN_DATA_PATH} did not retain InputConfig {INPUT_CONFIG_PATH}; "
            f"actual={configured_input}"
        ),
    )
    log(
        f"PawnData points at {PAWN_BLUEPRINT_PATH} with the top down camera "
        f"and InputConfig {INPUT_CONFIG_PATH}"
    )
    return pawn_data


def configure_game_feature_data(pawn_class, input_mapping):
    game_feature_data = require(
        load_existing(GAME_FEATURE_DATA_PATH),
        f"Missing GameFeatureData: {GAME_FEATURE_DATA_PATH}. Run CreateGameFeatureData first.",
    )

    manager_class = require_type("OceanWorldManagerComponent", "the OceanCore plugin")
    invoker_class = require_type("OceanChunkInvokerComponent", "the OceanCore plugin")
    chunk_class = require_type("OceanChunkActor", "the OceanCore plugin")
    hero_class = require_type("LyraHeroComponent", "the LyraGame module")
    presentation_class = require_type(
        "OceanChunkPresentationComponent", "the OceanAdventureRuntime module"
    )

    component_specs = [
        # The manager is the authority on which chunks exist. Clients get it too so they
        # can read the replicated WorldSeed and ChunkSize for local generation.
        (unreal.LyraGameState, manager_class),
        # LyraHeroComponent bridges PawnData to input and the camera mode stack. The
        # deliberately thin Ocean pawn does not own it as a construction-time default.
        (pawn_class, hero_class),
        # The invoker is what makes the player trigger chunk loading around themselves.
        # Target the C++ base rather than the blueprint so any pawn variant of this mode
        # gets it, and so the GameFeatureData does not depend on a specific content asset.
        (pawn_class, invoker_class),
        # OceanCore owns replicated chunk state; this feature supplies its concrete art.
        (chunk_class, presentation_class),
    ]

    # Replace only the named base actions owned by this script. Legacy unnamed actions and
    # hand-authored actions are preserved because their reflected component entries cannot be
    # inspected reliably from UE 5.7 Python.
    actions = [
        action
        for action in game_feature_data.get_editor_property("actions")
        if action is not None
        and str(action.get_name()) not in {
            BASE_COMPONENTS_ACTION_NAME,
            BASE_INPUT_MAPPING_ACTION_NAME,
        }
    ]
    actions.append(
        make_add_components_action(
            game_feature_data,
            component_specs,
            BASE_COMPONENTS_ACTION_NAME,
        )
    )
    actions.append(
        require(
            unreal.OceanAdventureAssetLibrary.create_add_input_context_mapping_action(
                game_feature_data,
                input_mapping,
                BASE_INPUT_MAPPING_PRIORITY,
                unreal.Name(BASE_INPUT_MAPPING_ACTION_NAME),
            ),
            "Failed to create OceanAdventure base input mapping action",
        )
    )
    game_feature_data.set_editor_property("actions", actions)

    save(GAME_FEATURE_DATA_PATH)
    log(
        "GameFeatureData injects the ocean world manager, Lyra hero bridge, "
        "chunk invoker, chunk presentation, and mouse-facing WASD mapping"
    )


def configure_experience(pawn_data):
    blueprint = require(
        load_existing(EXPERIENCE_PATH), f"Missing experience: {EXPERIENCE_PATH}"
    )
    action_sets = [
        require(load_existing(path), f"Missing action set: {path}")
        for path in ACTION_SET_PATHS
    ]

    experience = unreal.get_default_object(get_blueprint_class(blueprint, EXPERIENCE_PATH))
    experience.set_editor_property("default_pawn_data", pawn_data)
    experience.set_editor_property("game_features_to_enable", GAME_FEATURES_TO_ENABLE)
    experience.set_editor_property("action_sets", action_sets)

    # Blueprint defaults must be compiled into the generated class and the loaded
    # Blueprint asset itself must be saved. Reacquire the CDO after compilation because
    # compilation may replace the generated class.
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    configured_experience = unreal.get_default_object(
        get_blueprint_class(blueprint, EXPERIENCE_PATH)
    )
    configured_pawn_data = configured_experience.get_editor_property("default_pawn_data")
    configured_features = list(
        configured_experience.get_editor_property("game_features_to_enable")
    )
    configured_action_sets = list(configured_experience.get_editor_property("action_sets"))

    if configured_pawn_data != pawn_data:
        raise RuntimeError("Experience DefaultPawnData did not persist after compilation")
    if configured_features != GAME_FEATURES_TO_ENABLE:
        raise RuntimeError(
            f"Experience GameFeaturesToEnable did not persist: {configured_features}"
        )
    if configured_action_sets != action_sets:
        raise RuntimeError("Experience ActionSets did not persist after compilation")

    require(
        unreal.EditorAssetLibrary.save_loaded_asset(blueprint),
        f"Failed to save: {EXPERIENCE_PATH}",
    )
    log(f"Experience enables {', '.join(GAME_FEATURES_TO_ENABLE)}")


def main():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(["/OceanAdventure"], True, True)

    pawn_class = require_type("OceanAdventurePawn", "the OceanAdventureRuntime module")

    input_config, input_mapping = configure_input_assets()

    blueprint = get_or_create_blueprint(PAWN_BLUEPRINT_PATH, pawn_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    pawn_blueprint_class = get_blueprint_class(blueprint, PAWN_BLUEPRINT_PATH)
    configure_pawn_blueprint(pawn_blueprint_class)
    save(PAWN_BLUEPRINT_PATH)

    pawn_data = create_pawn_data(pawn_blueprint_class, input_config)
    configure_game_feature_data(pawn_class, input_mapping)
    configure_experience(pawn_data)

    log("Done. Restart the editor so the game features re-register with the new actions.")


main()
