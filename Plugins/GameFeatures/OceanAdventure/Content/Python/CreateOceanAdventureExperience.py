"""Create the Ocean Adventure pawn assets and wire them into the experience.

The pawn class itself carries no gameplay capability. This script builds the data
that turns it into the mode's player, and the wiring that injects everything else:

    BP_Experience_Ocean
      DefaultPawnData ------> DA_OceanAdventure_PawnData
                                PawnClass        -> BP_OceanAdventure_Pawn
                                AbilitySets      -> [] (intentional)
                                DefaultCameraMode-> ULyraCameraMode_TopDownFollow
      GameFeaturesToEnable -> "OceanAdventure", "TopDownFeature"

    OceanAdventure (GameFeatureData)
      AddComponents        -> LyraGameState        gets UOceanWorldManagerComponent
                              AOceanAdventurePawn  gets ULyraHeroComponent
                              AOceanAdventurePawn  gets UOceanChunkInvokerComponent
                              AOceanChunkActor     gets UOceanChunkPresentationComponent

    TopDownFeature (GameFeatureData, already configured)
      AddComponents        -> UTopDownPawnComponent, AddInputMapping -> IMC_TopDown

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

Idempotent: assets are created if missing, and their properties are rewritten either
way, so re-running repairs hand edits. Note that this rewrites the GameFeatureData's
Actions list, PawnData AbilitySets, and the experience's GameFeaturesToEnable / ActionSets
wholesale. Pawn movement tuning is stored on BP_OceanAdventure_Pawn, not its C++ base.
"""

import unreal


PAWN_BLUEPRINT_PATH = "/OceanAdventure/Character/BP_OceanAdventure_Pawn"
PAWN_DATA_PATH = "/OceanAdventure/Character/DA_OceanAdventure_PawnData"
GAME_FEATURE_DATA_PATH = "/OceanAdventure/OceanAdventure"
EXPERIENCE_PATH = "/OceanAdventure/Experience/BP_Experience_Ocean"

INPUT_CONFIG_PATH = "/Game/Input/DA_InputConfig_Base"
MAX_SWIM_SPEED = 600.0

ACTION_SET_PATHS = [
    "/Game/ActionSet/LSA_Standard_Components",
    "/Game/ActionSet/LSA_Shared_Input",
    "/Game/ActionSet/LAS_Standard_HUD",
]

# Plugin names, not paths: Lyra resolves these through GetPluginURLByName.
# OceanAdventure enables itself so its own AddComponents action runs, the same way
# Lyra's ShooterCore experiences do.
GAME_FEATURES_TO_ENABLE = ["OceanAdventure", "TopDownFeature"]


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
    # GameFeature packages can exist on disk before the commandlet's cached AssetRegistry
    # notices them. Loading by package path avoids a false "missing" result here.
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


def get_or_create_data_asset(asset_path, asset_class):
    existing = load_existing(asset_path)
    if existing is not None:
        log(f"Data asset already exists: {asset_path}")
        return existing

    package_path, asset_name = split_path(asset_path)
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


def make_add_components_action(outer, component_specs):
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
            outer, actor_classes, component_classes, True, True
        ),
        "Failed to create the AddComponents action",
    )


def create_pawn_data(pawn_blueprint_class):
    pawn_data = get_or_create_data_asset(PAWN_DATA_PATH, unreal.LyraPawnData)

    pawn_data.set_editor_property("pawn_class", pawn_blueprint_class)
    # This mode currently uses native movement/input only. Add a real Ocean-owned
    # AbilitySet when the mode gains abilities; do not reference a sibling GameFeature.
    pawn_data.set_editor_property("ability_sets", [])

    input_config = require(
        load_existing(INPUT_CONFIG_PATH), f"Missing input config: {INPUT_CONFIG_PATH}"
    )
    pawn_data.set_editor_property("input_config", input_config)

    # Comes from TopDownFeature, which this plugin already depends on in its .uplugin.
    # It is a native class, not plugin content, so it is not a cross-feature asset reference.
    camera_mode = require_type("LyraCameraMode_TopDownFollow", "the TopDownFeature plugin")
    pawn_data.set_editor_property("default_camera_mode", camera_mode)

    save(PAWN_DATA_PATH)
    log(
        f"PawnData points at {PAWN_BLUEPRINT_PATH} with the top down camera "
        "and intentionally empty AbilitySets"
    )
    return pawn_data


def configure_game_feature_data(pawn_class):
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

    action = make_add_components_action(game_feature_data, component_specs)
    game_feature_data.set_editor_property("actions", [action])

    save(GAME_FEATURE_DATA_PATH)
    log(
        "GameFeatureData injects the ocean world manager, Lyra hero bridge, "
        "chunk invoker, and chunk presentation"
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

    blueprint = get_or_create_blueprint(PAWN_BLUEPRINT_PATH, pawn_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    pawn_blueprint_class = get_blueprint_class(blueprint, PAWN_BLUEPRINT_PATH)
    configure_pawn_blueprint(pawn_blueprint_class)
    save(PAWN_BLUEPRINT_PATH)

    pawn_data = create_pawn_data(pawn_blueprint_class)
    configure_game_feature_data(pawn_class)
    configure_experience(pawn_data)

    log("Done. Restart the editor so the game features re-register with the new actions.")


main()
