"""Build the P0 greybox sea: two spawn bays, a central island, two side lanes, one beacon.

The P0 map has one job, from the design's minimum content list: give two to four players a
readable arena where a 6-8 minute match cannot stall. Everything here is a scaled engine
cube for that reason -- the shape of the space is what is being tested, not the art.

    two spawn bays        opposite ends, each with a player start and a raft
    central island        the only place worth being, and where the beacon stands
    two side lanes        a way around the middle that costs distance
    open water            room for the ships to actually manoeuvre

Every actor this creates is labelled, and a re-run destroys exactly those labels first, so
hand-placed additions survive.

    import BuildNavalP0Map
    BuildNavalP0Map.build()
"""

import unreal


MAP_PATH = "/OceanAdventure/Maps/L_NavalP0"
LABEL_PREFIX = "NavalP0_"

RAFT_ACTOR_PATH = "/Raft/Vehicles/Raft/BP_Raft_Default"
BEACON_ACTOR_PATH = "/OceanAdventure/Naval/BP_NavalP0_Beacon"
# The shared gun, in the general framework plugin: the same Blueprint the raft builds on
# its deck.
CANNON_BLUEPRINT_PATH = "/NavalCore/Naval/BP_Naval_Cannon"
CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"

# One unit of the arena. The two bays sit 24000cm apart, which is roughly 30 seconds of
# travel at the T0 raft's tuned top speed -- fast enough that the opening is not dead time.
BAY_OFFSET_X = 12000.0
LANE_OFFSET_Y = 9000.0
ISLAND_RADIUS = 3200.0
WATER_LEVEL = 0.0


def log(message):
    unreal.log(f"[NavalP0MapBuilder] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def load_class(path):
    return require(unreal.load_class(None, path), f"Unable to load class: {path}")


def load_asset(path):
    return require(unreal.EditorAssetLibrary.load_asset(path), f"Unable to load asset: {path}")


def load_blueprint_class(path):
    return require(
        unreal.EditorAssetLibrary.load_blueprint_class(path),
        f"Unable to load blueprint class: {path}; run CreateNavalP0Assets.py first",
    )


def load_or_create_map():
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    current_world = editor_subsystem.get_editor_world()
    current_map_path = current_world.get_path_name().split(".", 1)[0] if current_world else None

    if current_map_path == MAP_PATH:
        return level_subsystem

    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        require(level_subsystem.load_level(MAP_PATH), f"Unable to load {MAP_PATH}")
    else:
        require(level_subsystem.new_level(MAP_PATH), f"Unable to create {MAP_PATH}")
    return level_subsystem


def remove_previous_generated_actors(actor_subsystem):
    """Only this script's own actors, so anything placed by hand survives a re-run."""
    generated = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor.get_actor_label().startswith(LABEL_PREFIX)
    ]
    if generated:
        actor_subsystem.destroy_actors(generated)


def spawn(actor_subsystem, actor_class, label, location, rotation=None):
    actor = require(
        actor_subsystem.spawn_actor_from_class(actor_class, location, rotation or unreal.Rotator()),
        f"Unable to spawn {label}",
    )
    actor.set_actor_label(f"{LABEL_PREFIX}{label}")
    return actor


def spawn_block(actor_subsystem, label, location, scale, rotation=None):
    """A scaled engine cube. Greybox geometry, deliberately not art."""
    block = spawn(
        actor_subsystem, load_class("/Script/Engine.StaticMeshActor"), label, location, rotation
    )
    block.set_actor_scale3d(scale)
    mesh_component = require(
        block.get_component_by_class(load_class("/Script/Engine.StaticMeshComponent")),
        "StaticMeshActor has no StaticMeshComponent",
    )
    mesh_component.set_static_mesh(load_asset(CUBE_MESH_PATH))
    return block


def build_island(actor_subsystem):
    """The centre: worth holding, small enough that holding it can be contested."""
    # A shallow slab a character can walk on, sitting just above the waterline.
    spawn_block(
        actor_subsystem,
        "CentralIsland",
        unreal.Vector(0.0, 0.0, WATER_LEVEL + 60.0),
        unreal.Vector(ISLAND_RADIUS / 50.0, ISLAND_RADIUS / 50.0, 1.2),
    )

    # Two low ridges. They are cover, and they are also the reason a gun placed on the
    # island has a blind side worth walking around.
    for index, offset in enumerate((-1.0, 1.0)):
        spawn_block(
            actor_subsystem,
            f"IslandRidge_{index}",
            unreal.Vector(offset * ISLAND_RADIUS * 0.45, 0.0, WATER_LEVEL + 220.0),
            unreal.Vector(2.0, 14.0, 3.0),
        )


def build_lanes(actor_subsystem):
    """Two side lanes: a way past the middle that costs distance instead of risk."""
    for index, side in enumerate((-1.0, 1.0)):
        # A breakwater wall separating the lane from the central bowl.
        spawn_block(
            actor_subsystem,
            f"LaneWall_{index}",
            unreal.Vector(0.0, side * LANE_OFFSET_Y * 0.55, WATER_LEVEL + 240.0),
            unreal.Vector(90.0, 3.0, 5.0),
        )
        # An outer rock so the lane reads as a lane rather than as open sea.
        spawn_block(
            actor_subsystem,
            f"LaneRock_{index}",
            unreal.Vector(side * 2600.0, side * LANE_OFFSET_Y, WATER_LEVEL + 140.0),
            unreal.Vector(12.0, 8.0, 3.0),
        )


def build_bays(actor_subsystem):
    """Two mirrored spawn bays: a start, a raft, and a gun kit already on the beach."""
    player_start_class = load_class("/Script/LyraGame.LyraPlayerStart")
    raft_class = load_blueprint_class(RAFT_ACTOR_PATH)
    cannon_class = load_blueprint_class(CANNON_BLUEPRINT_PATH)

    for index, side in enumerate((-1.0, 1.0)):
        bay_x = side * BAY_OFFSET_X
        facing = unreal.Rotator(0.0, 0.0, 0.0 if side < 0 else 180.0)

        spawn_block(
            actor_subsystem,
            f"BayShore_{index}",
            unreal.Vector(bay_x + side * 2200.0, 0.0, WATER_LEVEL + 60.0),
            unreal.Vector(24.0, 60.0, 1.2),
        )
        spawn(
            actor_subsystem,
            player_start_class,
            f"PlayerStart_{index}",
            unreal.Vector(bay_x + side * 1800.0, 0.0, WATER_LEVEL + 200.0),
            facing,
        )
        # The T0 raft waits at the waterline: the first meaningful action is boarding it.
        spawn(
            actor_subsystem,
            raft_class,
            f"Raft_{index}",
            unreal.Vector(bay_x, 0.0, WATER_LEVEL),
            facing,
        )
        # One field gun per side, already deployed, so the light-versus-heavy exchange is
        # reachable in the first minute without waiting on an economy P0 does not have.
        spawn(
            actor_subsystem,
            cannon_class,
            f"GroundCannon_{index}",
            unreal.Vector(bay_x + side * 2400.0, 900.0, WATER_LEVEL + 130.0),
            facing,
        )


def build_beacon(actor_subsystem):
    spawn(
        actor_subsystem,
        load_blueprint_class(BEACON_ACTOR_PATH),
        "Beacon",
        unreal.Vector(0.0, 0.0, WATER_LEVEL + 200.0),
    )


def build_lighting(actor_subsystem):
    spawn(
        actor_subsystem,
        load_class("/Script/Engine.DirectionalLight"),
        "DirectionalLight",
        unreal.Vector(0.0, 0.0, 2000.0),
        unreal.Rotator(pitch=-48.0, yaw=-40.0, roll=0.0),
    )
    spawn(
        actor_subsystem,
        load_class("/Script/Engine.SkyLight"),
        "SkyLight",
        unreal.Vector(0.0, 0.0, 2000.0),
    )
    spawn(
        actor_subsystem,
        load_class("/Script/Engine.SkyAtmosphere"),
        "SkyAtmosphere",
        unreal.Vector(0.0, 0.0, 0.0),
    )


def build():
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        ["/OceanAdventure", "/Raft"], True, False
    )

    level_subsystem = load_or_create_map()
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    remove_previous_generated_actors(actor_subsystem)

    build_island(actor_subsystem)
    build_lanes(actor_subsystem)
    build_bays(actor_subsystem)
    build_beacon(actor_subsystem)
    build_lighting(actor_subsystem)

    require(level_subsystem.save_current_level(), f"Unable to save {MAP_PATH}")
    log(
        f"Built {MAP_PATH}. Set the map's default GameMode to Lyra's and its experience to "
        "B_Experience_NavalP0 in World Settings before PIE."
    )


if __name__ == "__main__":
    build()
