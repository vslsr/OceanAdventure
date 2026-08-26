"""One cannon, one player, nothing else: the smallest world that can reproduce firing.

The naval P0 experience runs a match component, a beacon, rafts, reconnect anchors and a
spawning manager. Every one of them can end the match, destroy a pawn, or clear an ability
set, and when firing misbehaves there is no way to tell which of them did it. This testbed
strips all of that out so a failure has exactly one place to come from.

    L_CannonTest                 flat slab, one PlayerStart, one ground cannon, lighting
    B_Experience_CannonTest      the OceanAdventure pawn and its naval abilities, and
                                 deliberately NO match component, beacon, raft, reconnect
                                 anchor table or spawning manager

What it deliberately keeps, because these are what firing actually runs on:
    OceanAdventure feature       IMC_OceanNaval, DA_InputConfig_OceanNaval, the naval
                                 ability set, the heavy weapon and its projectile
    TopDownFeature               the camera and the cursor aiming the gun reads

Run after CreateNavalP0Assets.py, which owns the cannon Blueprint and the ability set.
Safe to re-run: it destroys only the actors it labelled itself.

    import BuildCannonTestbed
    BuildCannonTestbed.build()
"""

import unreal


FEATURE_ROOT = "/OceanAdventure"
MAP_PATH = f"{FEATURE_ROOT}/Maps/L_CannonTest"
EXPERIENCE_PATH = f"{FEATURE_ROOT}/Experience/B_Experience_CannonTest"
PAWN_DATA_PATH = f"{FEATURE_ROOT}/Character/DA_OceanAdventure_PawnData"
# The shared gun, in the general framework plugin: the same Blueprint the raft builds on
# its deck, so this testbed measures the gun players actually get.
CANNON_BLUEPRINT_PATH = "/NavalCore/Naval/BP_Naval_Cannon"
CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"

LABEL_PREFIX = "CannonTest_"

# Raft is absent on purpose: a vessel in the level brings its own components and gives the
# match logic something to score, which is exactly the noise this map exists to remove.
GAME_FEATURES_TO_ENABLE = ["OceanAdventure", "TopDownFeature"]
ACTION_SET_PATHS = [
    "/Game/ActionSet/LSA_Standard_Components",
    "/Game/ActionSet/LSA_Shared_Input",
    "/Game/ActionSet/LAS_Standard_HUD",
]

# The slab is wider than the cannon's MaxRange so a full-charge shell lands on geometry
# rather than sailing off into empty space where nothing proves it arrived.
SLAB_HALF_EXTENT = 20000.0
SLAB_THICKNESS = 200.0
# Close enough that walking up and pressing E works immediately, far enough that the pawn
# does not spawn inside the gun's collision.
CANNON_OFFSET_X = 400.0


def log(message):
    unreal.log(f"[CannonTestbed] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def split_path(asset_path):
    package_path, _, asset_name = asset_path.rpartition("/")
    return package_path, asset_name


def load_class(path):
    return require(unreal.load_class(None, path), f"Unable to load class: {path}")


def load_asset(path):
    return require(unreal.EditorAssetLibrary.load_asset(path), f"Unable to load asset: {path}")


def load_blueprint_class(path):
    return require(
        unreal.EditorAssetLibrary.load_blueprint_class(path),
        f"Unable to load blueprint class: {path}; run CreateNavalP0Assets.py first",
    )


def save(asset):
    require(
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False),
        f"Unable to save {asset.get_path_name()}",
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


def require_type(type_name, source):
    return require(
        getattr(unreal, type_name, None),
        f"unreal.{type_name} is missing. Compile {source} and restart the editor.",
    )


def configure_experience():
    """Same pawn and features as the P0 match, minus everything that can end or reset it."""
    experience_class = require_type("LyraExperienceDefinition", "LyraGame")
    blueprint = get_or_create_blueprint(EXPERIENCE_PATH, experience_class)
    generated_class = blueprint_class(blueprint, EXPERIENCE_PATH)
    experience = unreal.get_default_object(generated_class)

    experience.set_editor_property(
        "default_pawn_data",
        require(
            unreal.EditorAssetLibrary.load_asset(PAWN_DATA_PATH),
            f"Missing {PAWN_DATA_PATH}; run CreateOceanAdventureExperience.py first",
        ),
    )
    experience.set_editor_property("game_features_to_enable", GAME_FEATURES_TO_ENABLE)
    experience.set_editor_property(
        "action_sets",
        [
            require(unreal.EditorAssetLibrary.load_asset(path), f"Missing action set: {path}")
            for path in ACTION_SET_PATHS
        ],
    )
    # No AddComponents actions at all. This is the whole point of the testbed: a match
    # component would end the match on a timer, and the reconnect/spawning pair would move
    # the pawn. Clearing the list keeps a re-run from inheriting either.
    experience.set_editor_property("actions", [])

    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    save(blueprint)

    configured = unreal.get_default_object(blueprint_class(blueprint, EXPERIENCE_PATH))
    require(
        len(configured.get_editor_property("actions")) == 0,
        "Cannon testbed experience still carries actions; a match or spawning component would "
        "reintroduce exactly the interference this map exists to exclude",
    )
    require(
        list(configured.get_editor_property("game_features_to_enable")) == GAME_FEATURES_TO_ENABLE,
        f"Cannon testbed experience did not retain {GAME_FEATURES_TO_ENABLE}",
    )
    log(f"Configured {EXPERIENCE_PATH} with no match, beacon, reconnect or spawning components")
    return blueprint_class(blueprint, EXPERIENCE_PATH)


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


def build_ground(actor_subsystem):
    """A single slab. Flat on purpose: the cannon refuses to deploy on a slope."""
    slab = spawn(
        actor_subsystem,
        load_class("/Script/Engine.StaticMeshActor"),
        "Ground",
        unreal.Vector(0.0, 0.0, -SLAB_THICKNESS * 0.5),
    )
    # The engine cube is 100cm, so scale is the extent in metres.
    slab.set_actor_scale3d(
        unreal.Vector(SLAB_HALF_EXTENT / 50.0, SLAB_HALF_EXTENT / 50.0, SLAB_THICKNESS / 100.0)
    )
    mesh_component = require(
        slab.get_component_by_class(load_class("/Script/Engine.StaticMeshComponent")),
        "StaticMeshActor has no StaticMeshComponent",
    )
    mesh_component.set_static_mesh(load_asset(CUBE_MESH_PATH))
    return slab


def build_player_start(actor_subsystem):
    return spawn(
        actor_subsystem,
        load_class("/Script/Engine.PlayerStart"),
        "PlayerStart",
        unreal.Vector(0.0, 0.0, 100.0),
    )


def build_cannon(actor_subsystem):
    """Pre-placed and already usable: deployment is a separate feature from firing."""
    return spawn(
        actor_subsystem,
        load_blueprint_class(CANNON_BLUEPRINT_PATH),
        "GroundCannon",
        unreal.Vector(CANNON_OFFSET_X, 0.0, 0.0),
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


def set_map_default_experience(level_subsystem, generated_class):
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = require(editor_subsystem.get_editor_world(), "No editor world after loading the map")
    world_settings = require(
        unreal.GameplayStatics.get_world_settings(world),
        f"Unable to reach the World Settings of {MAP_PATH}",
    )
    if not isinstance(world_settings, unreal.LyraWorldSettings):
        unreal.log_warning(
            f"[CannonTestbed] {MAP_PATH} uses {type(world_settings).__name__}, not "
            "LyraWorldSettings, so it has no DefaultGameplayExperience. Set the World "
            "Settings class in Project Settings > Engine > General Settings, then re-run."
        )
        return
    world_settings.set_editor_property("default_gameplay_experience", generated_class)
    log(f"{MAP_PATH}: DefaultGameplayExperience = B_Experience_CannonTest")


def build():
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [FEATURE_ROOT], True, False
    )

    # The experience first: the map's World Settings needs its generated class.
    generated_class = configure_experience()

    level_subsystem = load_or_create_map()
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    remove_previous_generated_actors(actor_subsystem)

    build_ground(actor_subsystem)
    build_player_start(actor_subsystem)
    build_cannon(actor_subsystem)
    build_lighting(actor_subsystem)

    set_map_default_experience(level_subsystem, generated_class)
    require(level_subsystem.save_current_level(), f"Unable to save {MAP_PATH}")
    log(
        f"Built {MAP_PATH}. PIE it, walk to the cannon, press E, then click the left mouse "
        "button once. A single click has to charge and fire."
    )
