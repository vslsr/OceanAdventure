"""Give the raft its naval layer: shootable build pieces and the vessel components.

Two things live here because both belong to the raft rather than to a game mode:

  * the P0 build pieces that can be shot -- wall, one-way firing window, pontoon, thruster,
    rudder and a deck cannon -- all backed by ANavalBuildPieceActor and tuned entirely
    through NavalCore's piece fragments;
  * Blueprint component subclasses carrying the T0 raft's hull, tonnage and helm placement,
    injected into ARaftActor by this feature's own GameFeatureData.

The vessel components are added by the Raft feature and not by a gameplay feature on purpose:
a GameFeature may not reference another GameFeature's classes or assets, and "how much hull a
T0 raft has" is a property of the raft anyway.

Run after compiling BuildingCoreRuntime, NavalCoreRuntime and RaftRuntime, and after
CreateRaftBuildPieceAssets.py, which owns the base module and the catalog. Safe to re-run.
"""

import unreal


FEATURE_ROOT = "/Raft"
PIECES_ROOT = f"{FEATURE_ROOT}/Build/Pieces"
NAVAL_ROOT = f"{FEATURE_ROOT}/Naval"
CATALOG_PATH = f"{FEATURE_ROOT}/Build/DA_BuildPieceCatalog_Raft"
GAME_FEATURE_DATA_PATH = f"{FEATURE_ROOT}/Raft"
BASE_MODULE_PIECE_PATH = f"{PIECES_ROOT}/DA_BuildPiece_Raft_Foundation_Wood"
INVALID_PREVIEW_MATERIAL_PATH = f"{FEATURE_ROOT}/Build/Materials/M_Raft_BuildPreview_Invalid"
CUBE_MESH_PATH = "/Engine/BasicShapes/Cube.Cube"
CYLINDER_MESH_PATH = "/Engine/BasicShapes/Cylinder.Cylinder"

NAVAL_PIECE_ACTOR_CLASS_PATH = "/Script/NavalCoreRuntime.NavalBuildPieceActor"
HEAVY_WEAPON_CLASS_PATH = "/Script/NavalCoreRuntime.NavalHeavyWeaponActor"
RAFT_ACTOR_CLASS_PATH = "/Script/RaftRuntime.RaftActor"
RAFT_DEFINITION_PATH = f"{FEATURE_ROOT}/Vehicles/Raft/DA_Raft_Default"
LIFE_RAFT_DEFINITION_PATH = f"{FEATURE_ROOT}/Vehicles/Raft/DA_Raft_LifeRaft"
LIFE_RAFT_BLUEPRINT_PATH = f"{FEATURE_ROOT}/Vehicles/Raft/BP_Raft_LifeRaft"

DECK_CANNON_BLUEPRINT_PATH = f"{NAVAL_ROOT}/BP_Raft_HeavyCannon"

# Design 7.11's starting tonnage scale: floor 1, wall/window 2, pontoon +2/+15,
# thruster +4/+18, small heavy weapon +8, large heavy weapon +12.
# Construction bands come from design 7.8: defensive 1.5-2.5s, mid function 2.5-4s,
# large function 4-6s, all including a 0.6-1.0s deploy windup.
PIECES = (
    {
        "asset": "DA_BuildPiece_Raft_Wall",
        "tag": "Raft.Piece.Wall.Plain",
        "slot": "WALL",
        "mesh": CUBE_MESH_PATH,
        "mesh_scale": (0.12, 2.0, 1.6),
        "mesh_offset": (0.0, 0.0, 80.0),
        "part_type": "WALL",
        "durability": 260.0,
        "deploy_seconds": 0.8,
        "construction_seconds": 1.4,
        "tonnage": 2.0,
        "buoyancy": 0.0,
        "thrust": 0.0,
        "steering": 0.0,
        "window": None,
    },
    {
        "asset": "DA_BuildPiece_Raft_FireWindow",
        "tag": "Raft.Piece.Wall.FireWindow",
        "slot": "WALL",
        "mesh": CUBE_MESH_PATH,
        "mesh_scale": (0.12, 2.0, 1.6),
        "mesh_offset": (0.0, 0.0, 80.0),
        "part_type": "FIRE_WINDOW",
        # Design 7.7: a window is 65%-75% of the same wall's durability and costs more.
        "durability": 185.0,
        "deploy_seconds": 0.8,
        "construction_seconds": 1.7,
        "tonnage": 2.0,
        "buoyancy": 0.0,
        "thrust": 0.0,
        "steering": 0.0,
        "window": {"half_angle": 40.0, "inner_depth": 260.0, "open_windup": 0.2},
    },
    {
        "asset": "DA_BuildPiece_Raft_Pontoon",
        "tag": "Raft.Piece.Prop.Pontoon",
        "slot": "PROP",
        "mesh": CYLINDER_MESH_PATH,
        "mesh_scale": (0.7, 0.7, 0.4),
        "mesh_offset": (0.0, 0.0, 20.0),
        "part_type": "PONTOON",
        "durability": 150.0,
        "deploy_seconds": 0.9,
        "construction_seconds": 2.3,
        "tonnage": 2.0,
        "buoyancy": 15.0,
        "thrust": 0.0,
        "steering": 0.0,
        "window": None,
    },
    {
        "asset": "DA_BuildPiece_Raft_Thruster",
        "tag": "Raft.Piece.Prop.Thruster",
        "slot": "PROP",
        "mesh": CUBE_MESH_PATH,
        "mesh_scale": (0.6, 0.4, 0.8),
        "mesh_offset": (0.0, 0.0, 40.0),
        "part_type": "PROPULSION",
        "durability": 170.0,
        "deploy_seconds": 0.9,
        "construction_seconds": 2.6,
        "tonnage": 4.0,
        "buoyancy": 0.0,
        "thrust": 18.0,
        "steering": 0.0,
        "window": None,
    },
    {
        "asset": "DA_BuildPiece_Raft_Rudder",
        "tag": "Raft.Piece.Prop.Rudder",
        "slot": "PROP",
        "mesh": CUBE_MESH_PATH,
        "mesh_scale": (0.5, 0.1, 0.7),
        "mesh_offset": (0.0, 0.0, 35.0),
        "part_type": "RUDDER",
        "durability": 140.0,
        "deploy_seconds": 0.8,
        "construction_seconds": 2.2,
        "tonnage": 2.0,
        "buoyancy": 0.0,
        "thrust": 0.0,
        "steering": 1.0,
        "window": None,
    },
)

# The base raft module is authored by CreateRaftBuildPieceAssets.py; this script only adds
# the naval load fragment it needs so an expanded deck actually weighs and floats something.
BASE_MODULE_LOAD = {"tonnage": 6.0, "buoyancy": 12.0, "thrust": 0.0, "steering": 0.0}

DECK_CANNON_PIECE = {
    "asset": "DA_BuildPiece_Raft_HeavyCannon",
    "tag": "Raft.Piece.Prop.HeavyCannon",
    "slot": "PROP",
    "mesh": CUBE_MESH_PATH,
    "mesh_scale": (1.2, 0.6, 0.6),
    "mesh_offset": (0.0, 0.0, 30.0),
    "tonnage": 12.0,
}

# Vessel component Blueprints, and the properties that make a T0 raft a T0 raft.
# Design 7.11's starting point for the core hull is W+24 / C+50 / P+40.
COMPONENT_BLUEPRINTS = (
    {
        "asset": "BPC_NavalVessel_RaftT0",
        "parent": "/Script/NavalCoreRuntime.NavalVesselComponent",
        "properties": {
            "max_hull_durability": 1200.0,
            "founder_seconds": 20.0,
            "emergency_repair_seconds": 7.0,
            "emergency_repair_hull_fraction": 0.12,
        },
    },
    {
        "asset": "BPC_NavalLoad_RaftT0",
        "parent": "/Script/NavalCoreRuntime.NavalLoadComponent",
        "properties": {
            "base_tonnage": 24.0,
            "base_buoyancy_capacity": 50.0,
            "base_thrust": 40.0,
        },
    },
    {
        "asset": "BPC_NavalHelm_RaftT0",
        "parent": "/Script/NavalCoreRuntime.NavalHelmComponent",
        "properties": {
            # 初始甲板中后部的固定格, with two open approaches so it can be reached and contested.
            "helm_local_offset": unreal.Vector(-120.0, 0.0, 25.0),
            "interaction_range": 260.0,
        },
    },
    {
        "asset": "BPC_NavalMovement_RaftT0",
        "parent": "/Script/NavalCoreRuntime.NavalMovementComponent",
        "properties": {
            "max_forward_speed": 900.0,
            "max_yaw_rate_degrees": 26.0,
        },
    },
)

OWNED_ACTION_NAME = "RaftNaval_AddVesselComponents"


def log(message):
    unreal.log(f"[RaftNavalAssets] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def require_type(type_name, hint):
    return require(getattr(unreal, type_name, None), f"{type_name} is unavailable; compile {hint} first")


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
            f"GameplayTag '{tag_name}' is not registered. Check Raft/Config/Tags/RaftTags.ini."
        )
    return tag


def validate_gameplay_tags():
    """Fail before touching assets if the Raft module did not register its tag directory."""
    tag_names = [spec["tag"] for spec in PIECES]
    tag_names.append(DECK_CANNON_PIECE["tag"])
    for tag_name in tag_names:
        gameplay_tag(tag_name)
    log(f"Validated {len(tag_names)} registered Raft naval piece tags")


def split_path(asset_path):
    return asset_path.rsplit("/", 1)


def save(asset):
    require(
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False),
        f"Unable to save {asset.get_path_name()}",
    )


def get_or_create_data_asset(asset_path, asset_class):
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return require(
            unreal.EditorAssetLibrary.load_asset(asset_path), f"Unable to load {asset_path}"
        )

    package_path, asset_name = split_path(asset_path)
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
        return require(
            unreal.EditorAssetLibrary.load_asset(asset_path), f"Unable to load {asset_path}"
        )

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


def enum_value(enum_type_name, value_name):
    enum_type = require_type(enum_type_name, "NavalCoreRuntime or BuildingCoreRuntime")
    return require(
        getattr(enum_type, value_name, None),
        f"{enum_type_name}.{value_name} is unavailable; recompile and restart the editor",
    )


def make_fragment(piece, fragment_type_name, properties):
    """Instanced fragments are outered to the piece that owns them."""
    fragment_class = require_type(fragment_type_name, "NavalCoreRuntime")
    fragment = require(
        unreal.new_object(fragment_class, piece), f"Unable to create {fragment_type_name}"
    )
    for name, value in properties.items():
        fragment.set_editor_property(name, value)
    return fragment


def replace_naval_fragments(piece, new_fragments):
    """Swap this script's fragments and keep anything another script authored."""
    owned_types = (
        "BuildPieceFragment_NavalLoad",
        "BuildPieceFragment_NavalPart",
        "BuildPieceFragment_NavalFireWindow",
    )
    owned_classes = tuple(
        require_type(type_name, "NavalCoreRuntime") for type_name in owned_types
    )
    kept = [
        fragment
        for fragment in piece.get_editor_property("fragments")
        if fragment is not None and not isinstance(fragment, owned_classes)
    ]
    piece.set_editor_property("fragments", kept + new_fragments)


def configure_naval_piece(piece_class, spec, invalid_material, naval_actor_class):
    asset_path = f"{PIECES_ROOT}/{spec['asset']}"
    piece = get_or_create_data_asset(asset_path, piece_class)

    piece.set_editor_property("piece_tag", gameplay_tag(spec["tag"]))
    piece.set_editor_property("slot_type", enum_value("BuildSlotType", spec["slot"]))
    piece.set_editor_property(
        "mesh", require(unreal.EditorAssetLibrary.load_asset(spec["mesh"]), f"Missing {spec['mesh']}")
    )
    piece.set_editor_property("mesh_scale", unreal.Vector(*spec["mesh_scale"]))
    piece.set_editor_property("mesh_offset", unreal.Vector(*spec["mesh_offset"]))
    piece.set_editor_property("footprint", unreal.IntPoint(1, 1))
    piece.set_editor_property("override_materials", [])
    piece.set_editor_property("invalid_preview_material", invalid_material)
    piece.set_editor_property("costs", [])

    # Every naval piece spawns an Actor: it has durability, a construction ramp and, for a
    # window, a team and an arc. A mesh instance could carry none of that.
    spawn_fragment = make_fragment(
        piece,
        "BuildPieceFragment_SpawnActor",
        {"actor_class": naval_actor_class, "spawn_offset": unreal.Vector(0.0, 0.0, 0.0)},
    )
    fragments = [
        spawn_fragment,
        make_fragment(
            piece,
            "BuildPieceFragment_NavalPart",
            {
                "part_type": enum_value("NavalPartType", spec["part_type"]),
                "max_durability": spec["durability"],
                "deploy_seconds": spec["deploy_seconds"],
                "construction_seconds": spec["construction_seconds"],
            },
        ),
        make_fragment(
            piece,
            "BuildPieceFragment_NavalLoad",
            {
                "tonnage": spec["tonnage"],
                "buoyancy_capacity": spec["buoyancy"],
                "thrust": spec["thrust"],
                "steering": spec["steering"],
            },
        ),
    ]
    if spec["window"] is not None:
        fragments.append(
            make_fragment(
                piece,
                "BuildPieceFragment_NavalFireWindow",
                {
                    "outward_half_angle_degrees": spec["window"]["half_angle"],
                    "inner_authorized_depth": spec["window"]["inner_depth"],
                    "open_windup_seconds": spec["window"]["open_windup"],
                },
            )
        )

    # The spawn fragment is not ours to keep exclusive, so rebuild the list explicitly.
    piece.set_editor_property("fragments", [])
    piece.set_editor_property("fragments", fragments)
    save(piece)
    return piece


def configure_deck_cannon(piece_class, invalid_material):
    """A deck-mounted heavy weapon, built as a piece and owned by the ship it stands on."""
    weapon_class = require(
        unreal.load_class(None, HEAVY_WEAPON_CLASS_PATH),
        f"Failed to load {HEAVY_WEAPON_CLASS_PATH}; compile NavalCoreRuntime first",
    )
    cannon_blueprint = get_or_create_blueprint(DECK_CANNON_BLUEPRINT_PATH, weapon_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(cannon_blueprint)
    save(cannon_blueprint)
    cannon_class = blueprint_class(cannon_blueprint, DECK_CANNON_BLUEPRINT_PATH)

    asset_path = f"{PIECES_ROOT}/{DECK_CANNON_PIECE['asset']}"
    piece = get_or_create_data_asset(asset_path, piece_class)
    piece.set_editor_property("piece_tag", gameplay_tag(DECK_CANNON_PIECE["tag"]))
    piece.set_editor_property("slot_type", enum_value("BuildSlotType", DECK_CANNON_PIECE["slot"]))
    piece.set_editor_property(
        "mesh",
        require(
            unreal.EditorAssetLibrary.load_asset(DECK_CANNON_PIECE["mesh"]),
            f"Missing {DECK_CANNON_PIECE['mesh']}",
        ),
    )
    piece.set_editor_property("mesh_scale", unreal.Vector(*DECK_CANNON_PIECE["mesh_scale"]))
    piece.set_editor_property("mesh_offset", unreal.Vector(*DECK_CANNON_PIECE["mesh_offset"]))
    piece.set_editor_property("footprint", unreal.IntPoint(1, 1))
    piece.set_editor_property("override_materials", [])
    piece.set_editor_property("invalid_preview_material", invalid_material)
    piece.set_editor_property("costs", [])

    # The weapon Actor owns its own durability and construction window, so the piece only
    # needs to say what it weighs.
    spawn_fragment = make_fragment(
        piece,
        "BuildPieceFragment_SpawnActor",
        {"actor_class": cannon_class, "spawn_offset": unreal.Vector(0.0, 0.0, 0.0)},
    )
    load_fragment = make_fragment(
        piece,
        "BuildPieceFragment_NavalLoad",
        {
            "tonnage": DECK_CANNON_PIECE["tonnage"],
            "buoyancy_capacity": 0.0,
            "thrust": 0.0,
            "steering": 0.0,
        },
    )
    piece.set_editor_property("fragments", [spawn_fragment, load_fragment])
    save(piece)
    return piece


def configure_base_module_load():
    """The base raft module weighs and floats something once naval load exists."""
    piece = unreal.EditorAssetLibrary.load_asset(BASE_MODULE_PIECE_PATH)
    if piece is None:
        log(f"{BASE_MODULE_PIECE_PATH} is missing; run CreateRaftBuildPieceAssets.py first")
        return

    load_fragment = make_fragment(
        piece,
        "BuildPieceFragment_NavalLoad",
        {
            "tonnage": BASE_MODULE_LOAD["tonnage"],
            "buoyancy_capacity": BASE_MODULE_LOAD["buoyancy"],
            "thrust": BASE_MODULE_LOAD["thrust"],
            "steering": BASE_MODULE_LOAD["steering"],
        },
    )
    replace_naval_fragments(piece, [load_fragment])
    save(piece)


def configure_life_raft():
    """The 折叠救生筏 hull: the same shape, with nothing that makes a ship a ship.

    It carries no build catalog, and that single omission is what enforces design 8.8's
    "no walls, no storage, no firing ports" -- UBuildStructureComponent refuses every
    placement when its catalog is missing, so there is no second rule to keep in sync.
    Hull and speed are scaled down by the ability that spawns it.
    """
    definition_class = require_type("RaftDefinition", "RaftRuntime")
    source = require(
        unreal.EditorAssetLibrary.load_asset(RAFT_DEFINITION_PATH),
        f"Missing {RAFT_DEFINITION_PATH}; run CreateRaftTestActor.py first",
    )
    life_raft_definition = get_or_create_data_asset(LIFE_RAFT_DEFINITION_PATH, definition_class)
    for property_name in (
		"visual_mesh",
		"deck_box_extent",
		"visual_mesh_offset",
        "pontoon_offsets",
        "waterline_offset",
        "max_tilt_degrees",
        "vertical_interp_speed",
        "rotation_interp_speed",
    ):
        life_raft_definition.set_editor_property(
            property_name, source.get_editor_property(property_name)
        )
    life_raft_definition.set_editor_property("build_piece_catalog", None)
    save(life_raft_definition)

    raft_class = require(
        unreal.load_class(None, RAFT_ACTOR_CLASS_PATH),
        f"Failed to load {RAFT_ACTOR_CLASS_PATH}; compile RaftRuntime first",
    )
    blueprint = get_or_create_blueprint(LIFE_RAFT_BLUEPRINT_PATH, raft_class)
    defaults = unreal.get_default_object(blueprint_class(blueprint, LIFE_RAFT_BLUEPRINT_PATH))
    defaults.set_editor_property("raft_definition", life_raft_definition)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    save(blueprint)
    log(
        f"Life raft ready at {LIFE_RAFT_BLUEPRINT_PATH}; point "
        "OceanAdventureNavalSettings.LifeRaftClass at it in Project Settings"
    )


def configure_component_blueprints():
    component_classes = []
    for spec in COMPONENT_BLUEPRINTS:
        asset_path = f"{NAVAL_ROOT}/{spec['asset']}"
        parent = require(
            unreal.load_class(None, spec["parent"]),
            f"Failed to load {spec['parent']}; compile NavalCoreRuntime first",
        )
        blueprint = get_or_create_blueprint(asset_path, parent)
        defaults = unreal.get_default_object(blueprint_class(blueprint, asset_path))
        for name, value in spec["properties"].items():
            defaults.set_editor_property(name, value)

        # Blueprint defaults only persist through a compile, and compilation can replace the
        # generated class, so the class is reacquired afterwards.
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        save(blueprint)
        component_classes.append(blueprint_class(blueprint, asset_path))
    return component_classes


def configure_game_feature_data(component_classes):
    game_feature_data = require(
        unreal.EditorAssetLibrary.load_asset(GAME_FEATURE_DATA_PATH),
        f"Missing {GAME_FEATURE_DATA_PATH}; run CreateRaftGameFeatureData.py first",
    )
    raft_class = require(
        unreal.load_class(None, RAFT_ACTOR_CLASS_PATH),
        f"Failed to load {RAFT_ACTOR_CLASS_PATH}; compile RaftRuntime first",
    )

    # Named so a re-run replaces this action instead of appending a duplicate, and so any
    # action configured by hand in the editor survives.
    actions = [
        action
        for action in game_feature_data.get_editor_property("actions")
        if action is not None and str(action.get_name()) != OWNED_ACTION_NAME
    ]
    actions.append(
        require(
            unreal.NavalCoreAssetLibrary.create_add_components_action(
                game_feature_data,
                [raft_class] * len(component_classes),
                component_classes,
                True,
                True,
                unreal.Name(OWNED_ACTION_NAME),
            ),
            "Failed to create the naval vessel AddComponents action",
        )
    )
    game_feature_data.set_editor_property("actions", actions)
    save(game_feature_data)

    names = {str(action.get_name()) for action in game_feature_data.get_editor_property("actions")}
    require(OWNED_ACTION_NAME in names, "GameFeatureData did not retain the naval components action")


def main():
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([FEATURE_ROOT], True, True)

    validate_gameplay_tags()

    piece_class = require_type("RaftBuildPieceDefinition", "RaftRuntime")
    catalog_class = require_type("BuildPieceCatalog", "BuildingCoreRuntime")
    naval_actor_class = require(
        unreal.load_class(None, NAVAL_PIECE_ACTOR_CLASS_PATH),
        f"Failed to load {NAVAL_PIECE_ACTOR_CLASS_PATH}; compile NavalCoreRuntime first",
    )
    invalid_material = require(
        unreal.EditorAssetLibrary.load_asset(INVALID_PREVIEW_MATERIAL_PATH),
        f"Missing {INVALID_PREVIEW_MATERIAL_PATH}; run CreateRaftBuildPieceAssets.py first",
    )

    managed_pieces = [
        configure_naval_piece(piece_class, spec, invalid_material, naval_actor_class)
        for spec in PIECES
    ]
    managed_pieces.append(configure_deck_cannon(piece_class, invalid_material))
    configure_base_module_load()

    catalog = get_or_create_data_asset(CATALOG_PATH, catalog_class)
    # Network indices are append-only: existing entries keep their index and new pieces are
    # only ever added at the end.
    pieces = list(catalog.get_editor_property("pieces"))
    for piece in managed_pieces:
        if piece not in pieces:
            pieces.append(piece)
    catalog.set_editor_property("pieces", pieces)
    save(catalog)

    configure_life_raft()
    configure_game_feature_data(configure_component_blueprints())

    log(
        f"Added {len(managed_pieces)} naval pieces to the raft catalog and injected the vessel "
        "components; restart the editor so the GameFeature re-registers"
    )


if __name__ == "__main__":
    main()
