"""Create the Raft-owned whole-module build piece and append-only catalog.

Run after compiling BuildingCoreRuntime and RaftRuntime. The assets stay inside the
Raft GameFeature and may be regenerated safely. The piece copies the mesh and alignment
from DA_Raft_Default, so building always adds another matching raft module.
"""

import unreal


PIECE_PATH = "/Raft/Build/Pieces/DA_BuildPiece_Raft_Foundation_Wood"
# Older runs created this second selectable entry. Network catalogs are append-only, so the
# script repairs it to the same whole-raft representation when present but does not create it
# in a new project.
DECK_PIECE_PATH = "/Raft/Build/Pieces/DA_BuildPiece_Raft_Deck"
PIECE_TAG = "Raft.Piece.Foundation.Wood"
DECK_PIECE_TAG = "Raft.Piece.Floor.Deck"
CAMPFIRE_PIECE_PATH = "/Raft/Build/Pieces/DA_BuildPiece_Raft_Campfire"
CAMPFIRE_PIECE_TAG = "Raft.Piece.Prop.Campfire"
CUBE_MESH_PATH = "/Engine/BasicShapes/Cube.Cube"
PLACED_ACTOR_CLASS_PATH = "/Script/BuildingCoreRuntime.BuildPlacedActor"
CATALOG_PATH = "/Raft/Build/DA_BuildPieceCatalog_Raft"
RAFT_DEFINITION_PATH = "/Raft/Vehicles/Raft/DA_Raft_Default"
INVALID_PREVIEW_MATERIAL_PATH = "/Raft/Build/Materials/M_Raft_BuildPreview_Invalid"


def gameplay_tag(tag_name):
    """Resolve a tag through the registry without constructing an unregistered loose tag."""
    request_tag = getattr(unreal.GameplayTagLibrary, "request_gameplay_tag", None)
    if request_tag is not None:
        tag = request_tag(unreal.Name(tag_name), False)
    else:
        # UE 5.7 does not expose FGameplayTag::RequestGameplayTag to Python. Importing
        # the struct text still routes through GameplayTagsManager::ImportSingleGameplayTag,
        # so an unregistered name remains invalid instead of creating a loose tag.
        tag = unreal.GameplayTag()
        tag.import_text(tag_name)

    if not unreal.GameplayTagLibrary.is_gameplay_tag_valid(tag):
        raise RuntimeError(
            f"GameplayTag '{tag_name}' is not registered. Check the feature's Config/Tags ini, "
            "or compile the module that declares it natively."
        )
    return tag


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def log(message):
    unreal.log(f"[RaftBuildPieceAssets] {message}")


def split_path(asset_path):
    return asset_path.rsplit("/", 1)


def get_or_create_data_asset(asset_path, asset_class):
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return require(
            unreal.EditorAssetLibrary.load_asset(asset_path),
            f"Unable to load {asset_path}",
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


def create_expression(material, expression_class, x, y):
    return require(
        unreal.MaterialEditingLibrary.create_material_expression(
            material, expression_class, x, y
        ),
        f"Unable to create {expression_class.__name__} in {material.get_path_name()}",
    )


def create_constant(material, value, x, y):
    expression = create_expression(
        material, unreal.MaterialExpressionConstant, x, y
    )
    expression.set_editor_property("r", value)
    return expression


def connect_required(from_expression, to_expression, to_input_name=""):
    require(
        unreal.MaterialEditingLibrary.connect_material_expressions(
            from_expression, "", to_expression, to_input_name
        ),
        (
            f"Unable to connect {from_expression.get_class().get_name()} to "
            f"{to_expression.get_class().get_name()} input '{to_input_name}'"
        ),
    )


def create_invalid_preview_material():
    if unreal.EditorAssetLibrary.does_asset_exist(INVALID_PREVIEW_MATERIAL_PATH):
        material = require(
            unreal.EditorAssetLibrary.load_asset(INVALID_PREVIEW_MATERIAL_PATH),
            f"Unable to load {INVALID_PREVIEW_MATERIAL_PATH}",
        )
    else:
        package_path, asset_name = split_path(INVALID_PREVIEW_MATERIAL_PATH)
        material = require(
            unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                asset_name,
                package_path,
                unreal.Material,
                unreal.MaterialFactoryNew(),
            ),
            f"Unable to create {INVALID_PREVIEW_MATERIAL_PATH}",
        )

    # Always rebuild the owned graph so rerunning this script repairs stale or
    # partially generated assets instead of silently accepting them.
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    # Depth testing stays ON. A depth-ignoring translucent slab draws over the whole
    # scene, so every valid/invalid transition read as a full-screen flash. The ghost
    # is lifted by UBuildPreviewComponent::PreviewLiftZ instead to avoid z-fighting.
    material.set_editor_property("disable_depth_test", False)

    time = create_expression(material, unreal.MaterialExpressionTime, -900, 120)
    speed_x = create_constant(material, 7.0, -900, 240)
    speed_y = create_constant(material, 9.3, -900, 420)
    time_x = create_expression(material, unreal.MaterialExpressionMultiply, -700, 160)
    time_y = create_expression(material, unreal.MaterialExpressionMultiply, -700, 380)
    sine_x = create_expression(material, unreal.MaterialExpressionSine, -500, 160)
    sine_y = create_expression(material, unreal.MaterialExpressionSine, -500, 380)

    amplitude = create_expression(
        material, unreal.MaterialExpressionScalarParameter, -500, 560
    )
    amplitude.set_editor_property("parameter_name", "ShakeAmplitude")
    amplitude.set_editor_property("default_value", 0.0)

    offset_x = create_expression(material, unreal.MaterialExpressionMultiply, -280, 180)
    offset_y = create_expression(material, unreal.MaterialExpressionMultiply, -280, 400)
    offset_xy = create_expression(material, unreal.MaterialExpressionAppendVector, -80, 260)
    zero = create_constant(material, 0.0, -80, 520)
    offset_xyz = create_expression(material, unreal.MaterialExpressionAppendVector, 130, 300)

    connect_required(time, time_x, "A")
    connect_required(speed_x, time_x, "B")
    connect_required(time, time_y, "A")
    connect_required(speed_y, time_y, "B")
    # UE5.7 exposes Sine's sole input as the unnamed first pin through
    # MaterialEditingLibrary. Asking for the reflected property name "Input"
    # fails silently and produces a default-material fallback at runtime.
    connect_required(time_x, sine_x)
    connect_required(time_y, sine_y)
    connect_required(sine_x, offset_x, "A")
    connect_required(amplitude, offset_x, "B")
    connect_required(sine_y, offset_y, "A")
    connect_required(amplitude, offset_y, "B")
    connect_required(offset_x, offset_xy, "A")
    connect_required(offset_y, offset_xy, "B")
    connect_required(offset_xy, offset_xyz, "A")
    connect_required(zero, offset_xyz, "B")
    require(
        unreal.MaterialEditingLibrary.connect_material_property(
            offset_xyz, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET
        ),
        "Unable to connect invalid-preview World Position Offset",
    )

    color = create_expression(
        material, unreal.MaterialExpressionConstant3Vector, -80, -180
    )
    color.set_editor_property("constant", unreal.LinearColor(1.0, 0.035, 0.01, 1.0))
    opacity = create_constant(material, 0.45, 140, -80)
    require(
        unreal.MaterialEditingLibrary.connect_material_property(
            color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
        ),
        "Unable to connect invalid-preview emissive color",
    )
    require(
        unreal.MaterialEditingLibrary.connect_material_property(
            opacity, "", unreal.MaterialProperty.MP_OPACITY
        ),
        "Unable to connect invalid-preview opacity",
    )
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(material)
    return material


def configure_raft_module(piece, piece_tag, raft_mesh, mesh_offset, invalid_material):
    """Make one catalog entry render exactly like the RaftDefinition's base module."""
    piece.set_editor_property("piece_tag", gameplay_tag(piece_tag))
    piece.set_editor_property("slot_type", unreal.BuildSlotType.FOUNDATION)
    piece.set_editor_property("mesh", raft_mesh)
    piece.set_editor_property("mesh_offset", mesh_offset)
    piece.set_editor_property("mesh_scale", unreal.Vector(1.0, 1.0, 1.0))
    piece.set_editor_property("footprint", unreal.IntPoint(1, 1))
    # Empty overrides preserve every material slot authored on SM_Raft.
    piece.set_editor_property("override_materials", [])
    piece.set_editor_property("invalid_preview_material", invalid_material)
    piece.set_editor_property("costs", [])
    save(piece)


def configure_campfire(piece, invalid_material):
    """Placeholder Prop that exercises the whole spawn-an-Actor path.

    A cube standing in a sub-cell, backed by the framework's ABuildPlacedActor. Replace the
    mesh and derive a Blueprint from that Actor when the real campfire exists; nothing in the
    framework has to change for that.
    """
    cube = require(
        unreal.EditorAssetLibrary.load_asset(CUBE_MESH_PATH),
        f"Missing {CUBE_MESH_PATH}",
    )
    actor_class = require(
        unreal.load_class(None, PLACED_ACTOR_CLASS_PATH),
        f"Failed to load {PLACED_ACTOR_CLASS_PATH}; compile BuildingCoreRuntime first",
    )
    fragment_class = require(
        getattr(unreal, "BuildPieceFragment_SpawnActor", None),
        "BuildPieceFragment_SpawnActor is unavailable; compile BuildingCoreRuntime first",
    )

    # Instanced fragments are outered to the piece that owns them.
    fragment = require(
        unreal.new_object(fragment_class, piece),
        "Unable to create the SpawnActor fragment",
    )
    fragment.set_editor_property("actor_class", actor_class)
    fragment.set_editor_property("spawn_offset", unreal.Vector(0.0, 0.0, 0.0))

    piece.set_editor_property("piece_tag", gameplay_tag(CAMPFIRE_PIECE_TAG))
    piece.set_editor_property("slot_type", unreal.BuildSlotType.PROP)
    piece.set_editor_property("mesh", cube)
    # The engine cube is 100cm; a 60cm block sitting on the deck reads as a placeholder.
    piece.set_editor_property("mesh_scale", unreal.Vector(0.6, 0.6, 0.6))
    piece.set_editor_property("mesh_offset", unreal.Vector(0.0, 0.0, 30.0))
    piece.set_editor_property("footprint", unreal.IntPoint(1, 1))
    piece.set_editor_property("override_materials", [])
    piece.set_editor_property("invalid_preview_material", invalid_material)
    piece.set_editor_property("costs", [])
    piece.set_editor_property("fragments", [fragment])
    save(piece)


def save(asset):
    require(
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False),
        f"Unable to save {asset.get_path_name()}",
    )


def main():
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        ["/Raft"], True, True
    )
    piece_class = require(
        getattr(unreal, "RaftBuildPieceDefinition", None),
        "RaftBuildPieceDefinition is unavailable; compile RaftRuntime and restart the editor",
    )
    catalog_class = require(
        getattr(unreal, "BuildPieceCatalog", None),
        "BuildPieceCatalog is unavailable; compile BuildingCoreRuntime and restart the editor",
    )
    invalid_preview_material = create_invalid_preview_material()

    raft_definition = require(
        unreal.EditorAssetLibrary.load_asset(RAFT_DEFINITION_PATH),
        f"Missing {RAFT_DEFINITION_PATH}; run CreateRaftTestActor once first",
    )
    raft_mesh = require(
        raft_definition.get_editor_property("visual_mesh"),
        f"{RAFT_DEFINITION_PATH} has no visual_mesh",
    )
    deck_extent = raft_definition.get_editor_property("deck_box_extent")
    visual_offset = raft_definition.get_editor_property("visual_mesh_offset")
    require(
        deck_extent.x > 0.0 and deck_extent.y > 0.0 and deck_extent.z > 0.0,
        f"{RAFT_DEFINITION_PATH} has an invalid deck_box_extent",
    )

    # A build slot's Z is the deck top, while the base VisualMesh transform is relative to
    # the collision centre. Subtract the deck-top height so the built instance receives the
    # exact same actor-relative transform as the original VisualMesh.
    module_mesh_offset = unreal.Vector(
        visual_offset.x,
        visual_offset.y,
        visual_offset.z - deck_extent.z,
    )

    piece = get_or_create_data_asset(PIECE_PATH, piece_class)
    configure_raft_module(
        piece,
        PIECE_TAG,
        raft_mesh,
        module_mesh_offset,
        invalid_preview_material,
    )

    managed_pieces = [piece]
    if unreal.EditorAssetLibrary.does_asset_exist(DECK_PIECE_PATH):
        legacy_deck_piece = require(
            unreal.EditorAssetLibrary.load_asset(DECK_PIECE_PATH),
            f"Unable to load {DECK_PIECE_PATH}",
        )
        configure_raft_module(
            legacy_deck_piece,
            DECK_PIECE_TAG,
            raft_mesh,
            module_mesh_offset,
            invalid_preview_material,
        )
        managed_pieces.append(legacy_deck_piece)

    campfire_piece = get_or_create_data_asset(CAMPFIRE_PIECE_PATH, piece_class)
    configure_campfire(campfire_piece, invalid_preview_material)
    managed_pieces.append(campfire_piece)

    catalog = get_or_create_data_asset(CATALOG_PATH, catalog_class)
    # Network indices are append-only. Keep every existing entry in place and only append a
    # managed asset when it has never been registered before.
    pieces = list(catalog.get_editor_property("pieces"))
    for new_piece in managed_pieces:
        if new_piece not in pieces:
            pieces.append(new_piece)
    catalog.set_editor_property("pieces", pieces)
    save(catalog)

    raft_definition.set_editor_property("build_piece_catalog", catalog)
    save(raft_definition)

    log(
        f"Configured {PIECE_PATH} from {raft_mesh.get_path_name()} with offset "
        f"{module_mesh_offset}; catalog remains append-only"
    )


if __name__ == "__main__":
    main()
