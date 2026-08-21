"""Create the Raft-owned P0 build piece and append-only catalog.

Run after compiling BuildingCoreRuntime and RaftRuntime. The assets stay inside the
Raft GameFeature and may be regenerated safely.
"""

import unreal


PIECE_PATH = "/Raft/Build/Pieces/DA_BuildPiece_Raft_Foundation_Wood"
DECK_PIECE_PATH = "/Raft/Build/Pieces/DA_BuildPiece_Raft_Deck"
RAFT_MESH_PATH = "/Raft/Vehicles/Raft/SM_Raft"
CELL_SIZE = 200.0
PIECE_TAG = "Raft.Piece.Foundation.Wood"
DECK_PIECE_TAG = "Raft.Piece.Floor.Deck"
CATALOG_PATH = "/Raft/Build/DA_BuildPieceCatalog_Raft"
RAFT_DEFINITION_PATH = "/Raft/Vehicles/Raft/DA_Raft_Default"
CUBE_PATH = "/Engine/BasicShapes/Cube.Cube"
WOOD_MATERIAL_PATH = "/Raft/Vehicles/Raft/M_Raft_Wood_Mid"
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


def cell_bottom_offset(mesh, scale):
    """Z offset that puts the scaled mesh's bottom face on the cell floor.

    Level 0 is the deck's top face now, so a piece only has to lift itself by its own
    half height instead of encoding the deck thickness in every asset.
    """
    bounds = mesh.get_bounds()
    return -(bounds.origin.z - bounds.box_extent.z) * scale.z


def fit_scale_to_cell(mesh, cell_size):
    """Uniformly maps a mesh's XY footprint onto exactly one build cell."""
    extent = mesh.get_bounds().box_extent
    return unreal.Vector(
        cell_size / max(1.0, extent.x * 2.0),
        cell_size / max(1.0, extent.y * 2.0),
        1.0,
    )


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
    cube = require(unreal.EditorAssetLibrary.load_asset(CUBE_PATH), f"Missing {CUBE_PATH}")
    wood_material = require(
        unreal.EditorAssetLibrary.load_asset(WOOD_MATERIAL_PATH),
        f"Missing {WOOD_MATERIAL_PATH}; run CreateRaftTestActor once first",
    )
    invalid_preview_material = create_invalid_preview_material()

    cube_scale = unreal.Vector(2.0, 2.0, 0.1)
    piece = get_or_create_data_asset(PIECE_PATH, piece_class)
    # BuildSelect looks pieces up by tag, so an untagged piece is unreachable from the GM.
    piece.set_editor_property("piece_tag", gameplay_tag(PIECE_TAG))
    piece.set_editor_property("mesh", cube)
    piece.set_editor_property(
        "mesh_offset", unreal.Vector(0.0, 0.0, cell_bottom_offset(cube, cube_scale))
    )
    piece.set_editor_property("mesh_scale", cube_scale)
    piece.set_editor_property("footprint", unreal.IntPoint(1, 1))
    piece.set_editor_property("override_materials", [wood_material])
    piece.set_editor_property("invalid_preview_material", invalid_preview_material)
    piece.set_editor_property("costs", [])
    save(piece)

    # Snap-built deck: the raft's own mesh mapped onto a single cell, so an extension
    # reads as more of the same raft rather than a foreign slab.
    raft_mesh = require(
        unreal.EditorAssetLibrary.load_asset(RAFT_MESH_PATH),
        f"Missing {RAFT_MESH_PATH}; run CreateRaftTestActor once first",
    )
    # Must match ARaftActor::BuildGridSettings.CellSize.
    deck_scale = fit_scale_to_cell(raft_mesh, CELL_SIZE)
    deck_piece = get_or_create_data_asset(DECK_PIECE_PATH, piece_class)
    deck_piece.set_editor_property("piece_tag", gameplay_tag(DECK_PIECE_TAG))
    deck_piece.set_editor_property("mesh", raft_mesh)
    deck_piece.set_editor_property(
        "mesh_offset", unreal.Vector(0.0, 0.0, cell_bottom_offset(raft_mesh, deck_scale))
    )
    deck_piece.set_editor_property("mesh_scale", deck_scale)
    deck_piece.set_editor_property("footprint", unreal.IntPoint(1, 1))
    deck_piece.set_editor_property("invalid_preview_material", invalid_preview_material)
    deck_piece.set_editor_property("costs", [])
    save(deck_piece)

    catalog = get_or_create_data_asset(CATALOG_PATH, catalog_class)
    # Network indices are append-only. P0 owns index zero and rewrites no later entry.
    pieces = list(catalog.get_editor_property("pieces"))
    for new_piece in (piece, deck_piece):
        if new_piece not in pieces:
            pieces.append(new_piece)
    catalog.set_editor_property("pieces", pieces)
    save(catalog)

    raft_definition = unreal.EditorAssetLibrary.load_asset(RAFT_DEFINITION_PATH)
    if raft_definition is not None:
        raft_definition.set_editor_property("build_piece_catalog", catalog)
        save(raft_definition)

    log(f"Configured {PIECE_PATH} and {CATALOG_PATH}")


if __name__ == "__main__":
    main()
