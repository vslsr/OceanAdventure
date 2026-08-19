"""Create the Ocean Adventure pawn assets and wire them into the experience.

The pawn class itself carries no gameplay capability. This script builds the data
that turns it into the mode's player, and the wiring that injects everything else:

    BP_Experience_Ocean
      DefaultPawnData ------> DA_OceanAdventure_PawnData
                                PawnClass        -> BP_OceanAdventure_Pawn
                                DefaultCameraMode-> ULyraCameraMode_TopDownFollow
      GameFeaturesToEnable -> "OceanAdventure", "TopDownFeature"

    OceanAdventure (GameFeatureData)
      AddComponents        -> LyraGameState        gets UOceanWorldManagerComponent
                              AOceanAdventurePawn  gets UOceanChunkInvokerComponent

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
Actions list and the experience's GameFeaturesToEnable / ActionSets wholesale.
"""

import unreal


PAWN_BLUEPRINT_PATH = "/OceanAdventure/Character/BP_OceanAdventure_Pawn"
PAWN_DATA_PATH = "/OceanAdventure/Character/DA_OceanAdventure_PawnData"
GAME_FEATURE_DATA_PATH = "/OceanAdventure/OceanAdventure"
EXPERIENCE_PATH = "/OceanAdventure/Experience/BP_Experience_Ocean"

INPUT_CONFIG_PATH = "/Game/Input/DA_InputConfig_Base"

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
    generated = blueprint.get_editor_property("generated_class")
    if generated is not None:
        return generated
    return require(
        unreal.EditorAssetLibrary.load_blueprint_class(asset_path),
        f"Unable to resolve the generated class of {asset_path}",
    )


def make_component_entry(actor_class, component_class):
    entry = unreal.GameFeatureComponentEntry()
    # ActorClass and ComponentClass are soft class properties: they take the class
    # itself, not a SoftClassPath wrapping its path.
    entry.set_editor_property("actor_class", actor_class)
    entry.set_editor_property("component_class", component_class)
    entry.set_editor_property("client_component", True)
    entry.set_editor_property("server_component", True)
    return entry


def make_add_components_action(outer, entries):
    action_class = require_type("GameFeatureAction_AddComponents", "the ModularGameplay plugin")
    # The action must be outered to the GameFeatureData it is stored on, otherwise saving
    # the package fails on a reference to a transient object.
    action = unreal.new_object(action_class, outer=outer)
    action.set_editor_property("component_list", entries)
    return action


def create_pawn_data(pawn_blueprint_class):
    pawn_data = get_or_create_data_asset(PAWN_DATA_PATH, unreal.LyraPawnData)

    pawn_data.set_editor_property("pawn_class", pawn_blueprint_class)

    input_config = require(
        load_existing(INPUT_CONFIG_PATH), f"Missing input config: {INPUT_CONFIG_PATH}"
    )
    pawn_data.set_editor_property("input_config", input_config)

    # Comes from TopDownFeature, which this plugin already depends on in its .uplugin.
    # It is a native class, not plugin content, so it is not a cross-feature asset reference.
    camera_mode = require_type("LyraCameraMode_TopDownFollow", "the TopDownFeature plugin")
    pawn_data.set_editor_property("default_camera_mode", camera_mode)

    save(PAWN_DATA_PATH)
    log(f"PawnData points at {PAWN_BLUEPRINT_PATH} with the top down camera")
    return pawn_data


def configure_game_feature_data(pawn_class):
    game_feature_data = require(
        load_existing(GAME_FEATURE_DATA_PATH),
        f"Missing GameFeatureData: {GAME_FEATURE_DATA_PATH}. Run CreateGameFeatureData first.",
    )

    manager_class = require_type("OceanWorldManagerComponent", "the OceanCore plugin")
    invoker_class = require_type("OceanChunkInvokerComponent", "the OceanCore plugin")

    entries = [
        # The manager is the authority on which chunks exist. Clients get it too so they
        # can read the replicated WorldSeed and ChunkSize for local generation.
        make_component_entry(unreal.LyraGameState, manager_class),
        # The invoker is what makes the player trigger chunk loading around themselves.
        # Target the C++ base rather than the blueprint so any pawn variant of this mode
        # gets it, and so the GameFeatureData does not depend on a specific content asset.
        make_component_entry(pawn_class, invoker_class),
    ]

    action = make_add_components_action(game_feature_data, entries)
    game_feature_data.set_editor_property("actions", [action])

    save(GAME_FEATURE_DATA_PATH)
    log("GameFeatureData injects the ocean world manager and the chunk invoker")


def configure_experience(pawn_data):
    blueprint = require(
        load_existing(EXPERIENCE_PATH), f"Missing experience: {EXPERIENCE_PATH}"
    )
    experience = unreal.get_default_object(get_blueprint_class(blueprint, EXPERIENCE_PATH))

    experience.set_editor_property("default_pawn_data", pawn_data)
    experience.set_editor_property("game_features_to_enable", GAME_FEATURES_TO_ENABLE)

    action_sets = [
        require(load_existing(path), f"Missing action set: {path}")
        for path in ACTION_SET_PATHS
    ]
    experience.set_editor_property("action_sets", action_sets)

    save(EXPERIENCE_PATH)
    log(f"Experience enables {', '.join(GAME_FEATURES_TO_ENABLE)}")


def main():
    pawn_class = require_type("OceanAdventurePawn", "the OceanAdventureRuntime module")

    blueprint = get_or_create_blueprint(PAWN_BLUEPRINT_PATH, pawn_class)
    save(PAWN_BLUEPRINT_PATH)
    pawn_blueprint_class = get_blueprint_class(blueprint, PAWN_BLUEPRINT_PATH)

    pawn_data = create_pawn_data(pawn_blueprint_class)
    configure_game_feature_data(pawn_class)
    configure_experience(pawn_data)

    log("Done. Restart the editor so the game features re-register with the new actions.")


main()
