"""Read-only validation for the creative raft-building MVP; never loads a map."""

import unreal


GAME_FEATURE_DATA_PATH = "/Raft/Raft"
PIECE_PATH = "/Raft/Build/Pieces/DA_BuildPiece_Raft_Foundation_Wood"
CATALOG_PATH = "/Raft/Build/DA_BuildPieceCatalog_Raft"
RAFT_DEFINITION_PATH = "/Raft/Vehicles/Raft/DA_Raft_Default"
RAFT_BLUEPRINT_PATH = "/Raft/Vehicles/Raft/BP_Raft_Default"
INVALID_PREVIEW_MATERIAL_PATH = "/Raft/Build/Materials/M_Raft_BuildPreview_Invalid"
PIECE_TAG = "Raft.Piece.Foundation.Wood"


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def load(asset_path):
    return require(
        unreal.EditorAssetLibrary.load_asset(asset_path),
        f"Missing or unloadable asset: {asset_path}",
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
    game_feature_data = load(GAME_FEATURE_DATA_PATH)
    invalid_preview_material = load(INVALID_PREVIEW_MATERIAL_PATH)

    require(piece.get_editor_property("mesh"), "MVP piece has no mesh")
    require(
        piece.get_editor_property("invalid_preview_material") == invalid_preview_material,
        "MVP piece does not reference the WPO invalid-preview material",
    )
    require(
        invalid_preview_material.get_editor_property("disable_depth_test"),
        "Invalid-preview material must ignore scene depth to avoid ocean/deck flicker",
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
    piece_tag = piece.get_editor_property("piece_tag")
    tag_name = str(piece_tag.get_editor_property("tag_name"))
    require(
        tag_name == PIECE_TAG,
        f"MVP piece has the wrong GameplayTag: {tag_name}",
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

    defaults = unreal.get_default_object(
        require(blueprint.generated_class(), "BP_Raft_Default has no generated class")
    )
    require(
        defaults.get_component_by_class(unreal.BuildStructureComponent),
        "BP_Raft_Default has no BuildStructureComponent",
    )
    require(
        defaults.get_component_by_class(unreal.BuildStructureVisualComponent),
        "BP_Raft_Default has no BuildStructureVisualComponent",
    )
    require(
        len(list(game_feature_data.get_editor_property("actions"))) > 0,
        "Raft GameFeatureData has no build-preview AddComponents action",
    )

    unreal.log("[RaftBuildMvpValidation] PASS")


if __name__ == "__main__":
    main()
