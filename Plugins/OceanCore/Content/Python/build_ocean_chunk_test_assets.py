import unreal


MAP_PATH = "/OceanCore/Maps/L_OceanChunkTest"
DEBUG_CHUNK_PATH = "/OceanCore/Blueprints/BP_OceanChunk_Debug"
MANAGER_COMPONENT_PATH = "/OceanCore/Blueprints/BP_OceanWorldManager_Debug"
TEST_INVOKER_PATH = "/OceanCore/Blueprints/BP_OceanChunk_TestInvoker"
SOURCE_MAP_PATH = "/Game/Map/L_Map_Default"


def log(message):
    unreal.log(f"[OceanCoreAssetBuilder] {message}")


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


def load_or_create_test_map():
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    current_world = editor_subsystem.get_editor_world()
    current_map_path = (
        current_world.get_path_name().split(".", 1)[0] if current_world else None
    )

    if current_map_path == MAP_PATH:
        log(f"Test map is already open: {MAP_PATH}")
        return level_subsystem

    if not unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        # duplicate_asset loads a UWorld for the destination package. Loading that same
        # package immediately afterwards triggers UE 5.7's world GC leak guard.
        if not level_subsystem.new_level_from_template(MAP_PATH, SOURCE_MAP_PATH):
            raise RuntimeError(
                f"Unable to create test map {MAP_PATH} from {SOURCE_MAP_PATH}"
            )
        return level_subsystem

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
    asset_registry.scan_paths_synchronous(["/OceanCore"], True, False)

    debug_chunk_class = configure_debug_chunk()
    manager_component_class = configure_manager_component(debug_chunk_class)
    test_invoker_class = configure_test_invoker()
    level_subsystem = load_or_create_test_map()
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    replace_test_invoker(actor_subsystem, test_invoker_class)
    if not level_subsystem.save_current_level():
        raise RuntimeError(f"Unable to save test map: {MAP_PATH}")

    log(f"Created editor test map: {MAP_PATH}")
    log(
        "Add the generated manager component class to LyraGameState with "
        "GameFeatureAction_AddComponents: "
        f"{manager_component_class.get_path_name()}"
    )


if __name__ == "__main__":
    build()
