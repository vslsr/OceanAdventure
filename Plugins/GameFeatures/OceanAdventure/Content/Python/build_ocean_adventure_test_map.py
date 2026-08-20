import unreal


MAP_PATH = "/OceanAdventure/Maps/L_OceanChunkTest"
GENERATED_ACTOR_LABELS = {
    "Ocean Test Floor",
    "Ocean Test Player Start",
    "Ocean Test Directional Light",
    "Ocean Test Sky Light",
}


def log(message):
    unreal.log(f"[OceanAdventureTestMapBuilder] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def load_class(path):
    return require(unreal.load_class(None, path), f"Unable to load class: {path}")


def load_asset(path):
    return require(
        unreal.EditorAssetLibrary.load_asset(path),
        f"Unable to load asset: {path}",
    )


def load_or_create_map():
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    current_world = editor_subsystem.get_editor_world()
    current_map_path = (
        current_world.get_path_name().split(".", 1)[0] if current_world else None
    )

    if current_map_path == MAP_PATH:
        return level_subsystem

    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        require(level_subsystem.load_level(MAP_PATH), f"Unable to load {MAP_PATH}")
    else:
        require(level_subsystem.new_level(MAP_PATH), f"Unable to create {MAP_PATH}")

    return level_subsystem


def remove_previous_generated_actors(actor_subsystem):
    actors_to_remove = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor.get_actor_label() in GENERATED_ACTOR_LABELS
    ]
    if actors_to_remove:
        actor_subsystem.destroy_actors(actors_to_remove)


def spawn_actor(actor_subsystem, class_path, label, location, rotation=None):
    actor_class = load_class(class_path)
    actor = actor_subsystem.spawn_actor_from_class(
        actor_class,
        location,
        rotation or unreal.Rotator(),
    )
    require(actor, f"Unable to spawn {label}")
    actor.set_actor_label(label)
    return actor


def create_test_floor(actor_subsystem):
    floor = spawn_actor(
        actor_subsystem,
        "/Script/Engine.StaticMeshActor",
        "Ocean Test Floor",
        # Kept well below the chunk terrain, which reaches about -350 at its lowest.
        # This used to sit at -50, putting its top face at exactly Z=0 -- the same plane as
        # every chunk's water mesh, which z-fought across the whole view and made the screen
        # flicker. The floor is only a safety net now; the terrain has BlockAll collision and
        # is what you actually stand on.
        unreal.Vector(0.0, 0.0, -3000.0),
    )
    # Engine BasicShapes/Cube is one meter wide, so this creates a 10 km floor.
    floor.set_actor_scale3d(unreal.Vector(10000.0, 10000.0, 1.0))

    mesh_component = require(
        floor.get_component_by_class(load_class("/Script/Engine.StaticMeshComponent")),
        "StaticMeshActor has no StaticMeshComponent",
    )
    mesh_component.set_static_mesh(load_asset("/Engine/BasicShapes/Cube"))


def create_player_start(actor_subsystem):
    spawn_actor(
        actor_subsystem,
        "/Script/LyraGame.LyraPlayerStart",
        "Ocean Test Player Start",
        unreal.Vector(0.0, 0.0, 150.0),
    )


def create_lighting(actor_subsystem):
    spawn_actor(
        actor_subsystem,
        "/Script/Engine.DirectionalLight",
        "Ocean Test Directional Light",
        unreal.Vector(0.0, 0.0, 500.0),
        unreal.Rotator(pitch=-45.0, yaw=-35.0, roll=0.0),
    )
    spawn_actor(
        actor_subsystem,
        "/Script/Engine.SkyLight",
        "Ocean Test Sky Light",
        unreal.Vector(0.0, 0.0, 500.0),
    )


def build():
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        ["/OceanAdventure"], True, False
    )

    level_subsystem = load_or_create_map()
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    remove_previous_generated_actors(actor_subsystem)
    create_test_floor(actor_subsystem)
    create_player_start(actor_subsystem)
    create_lighting(actor_subsystem)

    require(level_subsystem.save_current_level(), f"Unable to save {MAP_PATH}")
    log(f"Built clean Lyra integration map: {MAP_PATH}")
    log(
        "Open this map and use Lyra Developer Settings -> Experience Override "
        "to select the OceanAdventure experience."
    )


if __name__ == "__main__":
    build()
