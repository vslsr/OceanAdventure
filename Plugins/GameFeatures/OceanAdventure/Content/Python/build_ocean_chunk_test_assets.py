"""Build the grey-box chunk test blueprints and place an invoker in the test map.

These assets live in OceanAdventure, not OceanCore. OceanCore owns the algorithm and the
replicated chunk contract; anything you can see -- including debug visualisation -- is the
consuming game feature's. Keeping them here also means OceanCore never has to reference a
game feature, which the dependency rules forbid.

Owns:

    /OceanAdventure/Blueprints/BP_OceanChunk_Debug          draws chunk bounds
    /OceanAdventure/Blueprints/BP_OceanWorldManager_Debug   manager pointed at the above
    /OceanAdventure/Blueprints/BP_OceanChunk_TestInvoker    actor carrying an invoker
    the "Ocean Chunk Test Invoker" actor inside the test map

Does *not* own the map itself: build_ocean_adventure_test_map creates and furnishes
/OceanAdventure/Maps/L_OceanChunkTest. Run that one first.

BP_OceanWorldManager_Debug is for hand-testing only. The shipping path does not use it:
CreateOceanAdventureExperience injects the C++ UOceanWorldManagerComponent, and
UOceanChunkPresentationComponent supplies the terrain and water on the chunk actor.

Run from the editor Python console:

    py "<project>/Plugins/GameFeatures/OceanAdventure/Content/Python/build_ocean_chunk_test_assets.py"

Re-running rewrites the three blueprints' defaults and replaces the invoker actor, so hand
edits to those do not survive.
"""

import unreal


MAP_PATH = "/OceanAdventure/Maps/L_OceanChunkTest"
DEBUG_CHUNK_PATH = "/OceanAdventure/Blueprints/BP_OceanChunk_Debug"
MANAGER_COMPONENT_PATH = "/OceanAdventure/Blueprints/BP_OceanWorldManager_Debug"
TEST_INVOKER_PATH = "/OceanAdventure/Blueprints/BP_OceanChunk_TestInvoker"


def log(message):
    unreal.log(f"[OceanAdventureChunkAssetBuilder] {message}")


def load_class(path):
    loaded_class = unreal.load_class(None, path)
    if not loaded_class:
        raise RuntimeError(f"Unable to load class: {path}")
    return loaded_class


def create_or_load_blueprint(asset_path, parent_class):
    existing = None
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        existing = unreal.EditorAssetLibrary.load_asset(asset_path)
    if existing is not None:
        if not isinstance(existing, unreal.Blueprint):
            raise RuntimeError(f"Asset exists but is not a Blueprint: {asset_path}")
        return existing, False

    package_path, asset_name = asset_path.rsplit("/", 1)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name,
        package_path,
        unreal.Blueprint,
        factory,
    )
    if not blueprint:
        raise RuntimeError(f"Unable to create Blueprint: {asset_path}")
    return blueprint, True


def configure_debug_chunk():
    parent_class = load_class("/Script/OceanCoreRuntime.OceanChunkActor")
    blueprint, _ = create_or_load_blueprint(DEBUG_CHUNK_PATH, parent_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)

    defaults = unreal.get_default_object(blueprint.generated_class())
    defaults.set_editor_property("draw_debug_bounds", True)

    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
    log(f"Configured {DEBUG_CHUNK_PATH} to draw chunk bounds")
    return blueprint.generated_class()


def configure_manager_component(debug_chunk_class):
    parent_class = load_class(
        "/Script/OceanCoreRuntime.OceanWorldManagerComponent"
    )
    blueprint, _ = create_or_load_blueprint(MANAGER_COMPONENT_PATH, parent_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)

    defaults = unreal.get_default_object(blueprint.generated_class())
    defaults.set_editor_property("chunk_class", debug_chunk_class)
    defaults.set_editor_property("chunk_size", 20000.0)
    defaults.set_editor_property("unload_grace_seconds", 2.0)

    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
    log(f"Configured {MANAGER_COMPONENT_PATH} for Experience injection")
    return blueprint.generated_class()


def configure_test_invoker():
    parent_class = load_class("/Script/Engine.Actor")
    blueprint, was_created = create_or_load_blueprint(
        TEST_INVOKER_PATH, parent_class
    )

    if was_created:
        component_class = load_class(
            "/Script/OceanCoreRuntime.OceanChunkInvokerComponent"
        )
        subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        handles = subsystem.k2_gather_subobject_data_for_blueprint(blueprint)
        if not handles:
            raise RuntimeError("Unable to find the test invoker Blueprint root")

        params = unreal.AddNewSubobjectParams()
        params.set_editor_property("parent_handle", handles[0])
        params.set_editor_property("new_class", component_class)
        params.set_editor_property("blueprint_context", blueprint)
        _, failure_reason = subsystem.add_new_subobject(params)
        if str(failure_reason):
            raise RuntimeError(f"Unable to add invoker component: {failure_reason}")

    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
    log(f"Configured {TEST_INVOKER_PATH} with an OceanChunkInvokerComponent")
    return blueprint.generated_class()


def load_test_map():
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    current_world = editor_subsystem.get_editor_world()
    current_map_path = (
        current_world.get_path_name().split(".", 1)[0] if current_world else None
    )

    if current_map_path == MAP_PATH:
        log(f"Test map is already open: {MAP_PATH}")
        return level_subsystem

    # Deliberately does not create the map. build_ocean_adventure_test_map owns it, and two
    # scripts creating the same map from different templates would quietly fight.
    if not unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        raise RuntimeError(
            f"Test map {MAP_PATH} does not exist. Run build_ocean_adventure_test_map first."
        )

    if not level_subsystem.load_level(MAP_PATH):
        raise RuntimeError(f"Unable to load test map: {MAP_PATH}")
    return level_subsystem


def replace_test_invoker(actor_subsystem, test_invoker_class):
    actors = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor.get_actor_label() == "Ocean Chunk Test Invoker"
    ]
    if actors:
        actor_subsystem.destroy_actors(actors)

    invoker = actor_subsystem.spawn_actor_from_class(
        test_invoker_class, unreal.Vector(0.0, 0.0, 200.0), unreal.Rotator()
    )
    if not invoker:
        raise RuntimeError("Unable to spawn test invoker actor")
    invoker.set_actor_label("Ocean Chunk Test Invoker")


def build():
    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
    asset_registry.scan_paths_synchronous(["/OceanAdventure"], True, False)

    debug_chunk_class = configure_debug_chunk()
    manager_component_class = configure_manager_component(debug_chunk_class)
    test_invoker_class = configure_test_invoker()
    level_subsystem = load_test_map()
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    replace_test_invoker(actor_subsystem, test_invoker_class)
    if not level_subsystem.save_current_level():
        raise RuntimeError(f"Unable to save test map: {MAP_PATH}")

    log(f"Placed the test invoker in: {MAP_PATH}")
    log(
        "For hand testing, add this manager to LyraGameState yourself: "
        f"{manager_component_class.get_path_name()}. "
        "The experience injects the C++ UOceanWorldManagerComponent instead."
    )


if __name__ == "__main__":
    build()
