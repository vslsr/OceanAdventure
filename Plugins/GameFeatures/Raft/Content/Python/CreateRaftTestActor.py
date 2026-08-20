"""Import the default raft, build its data/Blueprint assets, and place it in OceanChunkTest.

Run after compiling RaftRuntime and creating /Raft/Raft:

    py "<project>/Plugins/GameFeatures/Raft/Content/Python/CreateRaftTestActor.py"

Re-running replaces only the actor labelled "Raft Test Actor" and rewrites the generated
Raft assets. The OceanAdventure test map remains owned by OceanAdventure.
"""

from pathlib import Path

import unreal


PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
SOURCE_FBX = PROJECT_ROOT / "Plugins" / "GameFeatures" / "Raft" / "ArtSource" / "Raft" / "SM_Raft.fbx"
STATIC_MESH_PATH = "/Raft/Vehicles/Raft/SM_Raft"
DEFINITION_PATH = "/Raft/Vehicles/Raft/DA_Raft_Default"
BLUEPRINT_PATH = "/Raft/Vehicles/Raft/BP_Raft_Default"
TEST_MAP_PATH = "/OceanAdventure/Maps/L_OceanChunkTest"
TEST_ACTOR_LABEL = "Raft Test Actor"

# SM_Raft is approximately 720 x 1202.5 x 55.9 cm. These values follow the
# authored UCX collision dimensions (708 x 1196 x 55 cm) and compensate for
# the FBX origin being 12.5 cm below the collision center.
DECK_BOX_EXTENT = unreal.Vector(354.0, 598.0, 27.5)
VISUAL_MESH_OFFSET = unreal.Vector(0.0, 0.0, 12.5)
PONTOON_OFFSETS = (
    unreal.Vector(285.0, 480.0, 0.0),
    unreal.Vector(-285.0, 480.0, 0.0),
    unreal.Vector(285.0, -480.0, 0.0),
    unreal.Vector(-285.0, -480.0, 0.0),
)


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def log(message):
    unreal.log(f"[RaftTestActorBuilder] {message}")


def split_asset_path(asset_path):
    return asset_path.rsplit("/", 1)


def import_static_mesh():
    if unreal.EditorAssetLibrary.does_asset_exist(STATIC_MESH_PATH):
        existing = require(
            unreal.EditorAssetLibrary.load_asset(STATIC_MESH_PATH),
            f"Unable to load {STATIC_MESH_PATH}",
        )
        log(f"Reusing {STATIC_MESH_PATH}")
        return existing

    require(SOURCE_FBX.is_file(), f"Missing raft FBX: {SOURCE_FBX}")

    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    static_options = options.get_editor_property("static_mesh_import_data")
    static_options.set_editor_property("combine_meshes", True)

    destination_path, destination_name = split_asset_path(STATIC_MESH_PATH)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(SOURCE_FBX))
    task.set_editor_property("destination_path", destination_path)
    task.set_editor_property("destination_name", destination_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    mesh = require(
        unreal.EditorAssetLibrary.load_asset(STATIC_MESH_PATH),
        f"Unable to import {STATIC_MESH_PATH}",
    )
    log(f"Imported {STATIC_MESH_PATH}")
    return mesh


def get_or_create_data_asset(asset_path, asset_class):
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return require(
            unreal.EditorAssetLibrary.load_asset(asset_path),
            f"Unable to load {asset_path}",
        )

    package_path, asset_name = split_asset_path(asset_path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    return require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, asset_class, factory
        ),
        f"Unable to create {asset_path}",
    )


def configure_definition(mesh):
    definition_class = require(
        getattr(unreal, "RaftDefinition", None),
        "RaftDefinition is unavailable; compile RaftRuntime and restart the editor",
    )
    definition = get_or_create_data_asset(DEFINITION_PATH, definition_class)
    definition.set_editor_property("visual_mesh", mesh)
    definition.set_editor_property("deck_box_extent", DECK_BOX_EXTENT)
    definition.set_editor_property("visual_mesh_offset", VISUAL_MESH_OFFSET)
    definition.set_editor_property("pontoon_offsets", list(PONTOON_OFFSETS))
    definition.set_editor_property("waterline_offset", 0.0)
    definition.set_editor_property("max_tilt_degrees", 5.0)
    definition.set_editor_property("vertical_interp_speed", 3.0)
    definition.set_editor_property("rotation_interp_speed", 2.0)
    require(
        unreal.EditorAssetLibrary.save_loaded_asset(definition, only_if_is_dirty=False),
        f"Unable to save {DEFINITION_PATH}",
    )
    return definition


def get_or_create_blueprint(parent_class):
    if unreal.EditorAssetLibrary.does_asset_exist(BLUEPRINT_PATH):
        existing = require(
            unreal.EditorAssetLibrary.load_asset(BLUEPRINT_PATH),
            f"Unable to load {BLUEPRINT_PATH}",
        )
        require(isinstance(existing, unreal.Blueprint), f"Not a Blueprint: {BLUEPRINT_PATH}")
        return existing

    package_path, asset_name = split_asset_path(BLUEPRINT_PATH)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    return require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, unreal.Blueprint, factory
        ),
        f"Unable to create {BLUEPRINT_PATH}",
    )


def configure_blueprint(definition):
    parent_class = require(
        getattr(unreal, "RaftActor", None),
        "RaftActor is unavailable; compile RaftRuntime and restart the editor",
    )
    blueprint = get_or_create_blueprint(parent_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    defaults = unreal.get_default_object(blueprint.generated_class())
    defaults.set_editor_property("raft_definition", definition)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    require(
        unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False),
        f"Unable to save {BLUEPRINT_PATH}",
    )
    return blueprint.generated_class()


def load_test_map():
    require(
        unreal.EditorAssetLibrary.does_asset_exist(TEST_MAP_PATH),
        f"Missing test map: {TEST_MAP_PATH}",
    )
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor_subsystem.get_editor_world()
    current_map = world.get_path_name().split(".", 1)[0] if world else None
    if current_map != TEST_MAP_PATH:
        require(level_subsystem.load_level(TEST_MAP_PATH), f"Unable to load {TEST_MAP_PATH}")
    return level_subsystem


def place_test_actor(raft_class, level_subsystem):
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    previous = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor.get_actor_label() == TEST_ACTOR_LABEL
    ]
    if previous:
        actor_subsystem.destroy_actors(previous)

    raft = require(
        actor_subsystem.spawn_actor_from_class(
            raft_class, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator()
        ),
        "Unable to spawn the raft test actor",
    )
    raft.set_actor_label(TEST_ACTOR_LABEL)
    require(level_subsystem.save_current_level(), f"Unable to save {TEST_MAP_PATH}")
    log(f"Placed {TEST_ACTOR_LABEL} in {TEST_MAP_PATH}")


def main():
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        ["/Raft", "/OceanAdventure"], True, True
    )
    mesh = import_static_mesh()
    definition = configure_definition(mesh)
    raft_class = configure_blueprint(definition)
    level_subsystem = load_test_map()
    place_test_actor(raft_class, level_subsystem)
    log("Done")


if __name__ == "__main__":
    main()
