"""Read-only validation for whole-module raft building; never loads a map."""

import unreal


GAME_FEATURE_DATA_PATH = "/Raft/Raft"
PIECE_PATH = "/Raft/Build/Pieces/DA_BuildPiece_Raft_Foundation_Wood"
LEGACY_DECK_PIECE_PATH = "/Raft/Build/Pieces/DA_BuildPiece_Raft_Deck"
CATALOG_PATH = "/Raft/Build/DA_BuildPieceCatalog_Raft"
RAFT_DEFINITION_PATH = "/Raft/Vehicles/Raft/DA_Raft_Default"
RAFT_BLUEPRINT_PATH = "/Raft/Vehicles/Raft/BP_Raft_Default"
INVALID_PREVIEW_MATERIAL_PATH = "/Raft/Build/Materials/M_Raft_BuildPreview_Invalid"
PIECE_TAG = "Raft.Piece.Foundation.Wood"
EXPECTED_DECK_BOX_EXTENT = unreal.Vector(100.0, 100.0, 75.0)
EXPECTED_VISUAL_MESH_OFFSET = unreal.Vector(0.0, 0.0, 0.0)


def gameplay_tag(tag_name):
    request_tag = getattr(unreal.GameplayTagLibrary, "request_gameplay_tag", None)
    if request_tag is not None:
        tag = request_tag(unreal.Name(tag_name), False)
    else:
        tag = unreal.GameplayTag()
        tag.import_text(tag_name)

    require(
        unreal.GameplayTagLibrary.is_gameplay_tag_valid(tag),
        f"GameplayTag '{tag_name}' is not registered; check Raft/Config/Tags/RaftTags.ini",
    )
    return tag


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def load(asset_path):
    return require(
        unreal.EditorAssetLibrary.load_asset(asset_path),
        f"Missing or unloadable asset: {asset_path}",
    )


def vector_nearly_equal(actual, expected, tolerance=0.01):
    return (
        abs(actual.x - expected.x) <= tolerance
        and abs(actual.y - expected.y) <= tolerance
        and abs(actual.z - expected.z) <= tolerance
    )


def validate_module_piece(piece, raft_mesh, expected_mesh_offset):
    require(
        piece.get_editor_property("slot_type") == unreal.BuildSlotType.FOUNDATION,
        f"{piece.get_path_name()} is not a Foundation slot",
    )
    require(
        piece.get_editor_property("mesh") == raft_mesh,
        f"{piece.get_path_name()} does not use the RaftDefinition visual mesh",
    )
    require(
        vector_nearly_equal(
            piece.get_editor_property("mesh_scale"), unreal.Vector(1.0, 1.0, 1.0)
        ),
        f"{piece.get_path_name()} scales the raft instead of using the original model size",
    )
    require(
        vector_nearly_equal(piece.get_editor_property("mesh_offset"), expected_mesh_offset),
        f"{piece.get_path_name()} is not aligned with the base raft VisualMesh transform",
    )
    footprint = piece.get_editor_property("footprint")
    require(
        footprint.x == 1 and footprint.y == 1,
        f"{piece.get_path_name()} must occupy one whole-raft grid cell",
    )
    require(
        len(list(piece.get_editor_property("override_materials"))) == 0,
        f"{piece.get_path_name()} overrides SM_Raft materials",
    )


def collect_input_graph(material, root):
    visited = set()
    pending = [root]
    result = []
    while pending:
        expression = pending.pop()
        expression_path = expression.get_path_name()
        if expression_path in visited:
            continue
        visited.add(expression_path)
        result.append(expression)
        pending.extend(
            unreal.MaterialEditingLibrary.get_inputs_for_material_expression(
                material, expression
            )
        )
    return result


def main():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(["/Raft"], True, True)

    piece = load(PIECE_PATH)
    catalog = load(CATALOG_PATH)
    definition = load(RAFT_DEFINITION_PATH)
    blueprint = load(RAFT_BLUEPRINT_PATH)
    load(GAME_FEATURE_DATA_PATH)
    invalid_preview_material = load(INVALID_PREVIEW_MATERIAL_PATH)

    defaults = unreal.get_default_object(
        require(blueprint.generated_class(), "BP_Raft_Default has no generated class")
    )
    deck_collision = require(
        defaults.get_deck_collision(), "BP_Raft_Default has no DeckCollision"
    )
    visual_pivot = require(defaults.get_visual_pivot(), "BP_Raft_Default has no VisualPivot")
    visual_mesh = require(defaults.get_visual_mesh(), "BP_Raft_Default has no VisualMesh")
    raft_mesh = require(
        visual_mesh.get_editor_property("static_mesh"),
        "BP_Raft_Default VisualMesh has no static mesh",
    )
    deck_extent = deck_collision.get_unscaled_box_extent()
    visual_offset = visual_pivot.get_editor_property("relative_location")
    require(
        vector_nearly_equal(deck_extent, EXPECTED_DECK_BOX_EXTENT),
        "BP_Raft_Default must use the 200 x 200 x 150 cm DeckCollision envelope",
    )
    require(
        vector_nearly_equal(visual_offset, EXPECTED_VISUAL_MESH_OFFSET),
        "BP_Raft_Default must keep VisualPivot at the collision-box centre",
    )
    expected_mesh_offset = unreal.Vector(
        visual_offset.x,
        visual_offset.y,
        visual_offset.z - deck_extent.z,
    )
    validate_module_piece(piece, raft_mesh, expected_mesh_offset)
    if unreal.EditorAssetLibrary.does_asset_exist(LEGACY_DECK_PIECE_PATH):
        validate_module_piece(
            load(LEGACY_DECK_PIECE_PATH), raft_mesh, expected_mesh_offset
        )
    require(
        piece.get_editor_property("invalid_preview_material") == invalid_preview_material,
        "MVP piece does not reference the WPO invalid-preview material",
    )
    wpo_root = require(
        unreal.MaterialEditingLibrary.get_material_property_input_node(
            invalid_preview_material,
            unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET,
        ),
        "Invalid-preview material has no World Position Offset input",
    )
    wpo_graph = collect_input_graph(invalid_preview_material, wpo_root)
    sine_nodes = [
        expression
        for expression in wpo_graph
        if isinstance(expression, unreal.MaterialExpressionSine)
    ]
    require(
        len(sine_nodes) == 2,
        "Invalid-preview WPO must contain two connected Sine nodes",
    )
    for sine_node in sine_nodes:
        require(
            len(
                unreal.MaterialEditingLibrary.get_inputs_for_material_expression(
                    invalid_preview_material, sine_node
                )
            )
            == 1,
            "Invalid-preview Sine node has no connected input",
        )
    scalar_parameter_names = {
        str(name)
        for name in unreal.MaterialEditingLibrary.get_scalar_parameter_names(
            invalid_preview_material
        )
    }
    require(
        "ShakeAmplitude" in scalar_parameter_names,
        "Invalid-preview material has no ShakeAmplitude scalar parameter",
    )
    require(
        unreal.MaterialEditingLibrary.get_material_default_scalar_parameter_value(
            invalid_preview_material, "ShakeAmplitude"
        )
        == 0.0,
        "Invalid-preview material must remain still until a failed click",
    )
    require(
        not invalid_preview_material.get_editor_property("disable_depth_test"),
        "Invalid-preview material must keep depth testing on, or the ghost draws over the whole scene",
    )
    expected_tag = gameplay_tag(PIECE_TAG)
    require(
        unreal.GameplayTagLibrary.equal_equal_gameplay_tag(
            piece.get_editor_property("piece_tag"), expected_tag
        ),
        f"MVP piece does not carry {PIECE_TAG}",
    )
    catalog_pieces = list(catalog.get_editor_property("pieces"))
    require(
        len(catalog_pieces) >= 1 and catalog_pieces[0] == piece,
        "Raft build catalog index zero must stay the wooden foundation (indices are append-only)",
    )
    require(
        definition.get_editor_property("build_piece_catalog") == catalog,
        "DA_Raft_Default does not reference the Raft build catalog",
    )

    require(
        defaults.get_component_by_class(unreal.BuildStructureComponent),
        "BP_Raft_Default has no BuildStructureComponent",
    )
    require(
        defaults.get_component_by_class(unreal.BuildStructureVisualComponent),
        "BP_Raft_Default has no BuildStructureVisualComponent",
    )
    unreal.log("[RaftBuildMvpValidation] PASS")


if __name__ == "__main__":
    main()
