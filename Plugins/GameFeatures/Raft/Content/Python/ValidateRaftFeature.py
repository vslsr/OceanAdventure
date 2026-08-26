"""Validate the generated Raft assets and the OceanAdventure test-map integration.

Run from Unreal Editor or UnrealEditor-Cmd after BuildRaftFeature.py:

    py "<project>/Plugins/GameFeatures/Raft/Content/Python/ValidateRaftFeature.py"

This script is read-only: it loads assets and the test map, but never saves them.
"""

import unreal


GAME_FEATURE_DATA_PATH = "/Raft/Raft"
STATIC_MESH_PATH = "/Raft/Vehicles/Raft/SM_Raft"
DEFINITION_PATH = "/Raft/Vehicles/Raft/DA_Raft_Default"
BLUEPRINT_PATH = "/Raft/Vehicles/Raft/BP_Raft_Default"
EXPERIENCE_PATH = "/OceanAdventure/Experience/BP_Experience_Ocean"
TEST_MAP_PATH = "/OceanAdventure/Maps/L_OceanChunkTest"
TEST_ACTOR_LABEL = "Raft Test Actor"
EXPECTED_DECK_BOX_EXTENT = unreal.Vector(100.0, 100.0, 75.0)
EXPECTED_VISUAL_MESH_OFFSET = unreal.Vector(0.0, 0.0, 0.0)
EXPECTED_PONTOON_OFFSETS = (
    unreal.Vector(80.0, 80.0, 0.0),
    unreal.Vector(-80.0, 80.0, 0.0),
    unreal.Vector(80.0, -80.0, 0.0),
    unreal.Vector(-80.0, -80.0, 0.0),
)


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def load(asset_path):
    require(
        unreal.EditorAssetLibrary.does_asset_exist(asset_path),
        f"Missing asset: {asset_path}",
    )
    return require(
        unreal.EditorAssetLibrary.load_asset(asset_path),
        f"Unable to load: {asset_path}",
    )


def vector_nearly_equal(actual, expected, tolerance=0.01):
    return (
        abs(actual.x - expected.x) <= tolerance
        and abs(actual.y - expected.y) <= tolerance
        and abs(actual.z - expected.z) <= tolerance
    )


def validate_assets():
    game_feature_data = load(GAME_FEATURE_DATA_PATH)
    game_feature_data_class = game_feature_data.get_class().get_name()
    require(
        game_feature_data_class == "GameFeatureData",
        f"Not a GameFeatureData: {GAME_FEATURE_DATA_PATH} ({game_feature_data_class})",
    )

    mesh = load(STATIC_MESH_PATH)
    definition = load(DEFINITION_PATH)
    require(
        definition.get_editor_property("visual_mesh") == mesh,
        "Raft definition does not reference the imported static mesh",
    )
    require(
        vector_nearly_equal(
            definition.get_editor_property("deck_box_extent"),
            EXPECTED_DECK_BOX_EXTENT,
        ),
        "Raft deck collision extent does not match the current mesh",
    )
    require(
        vector_nearly_equal(
            definition.get_editor_property("visual_mesh_offset"),
            EXPECTED_VISUAL_MESH_OFFSET,
        ),
        "Raft visual offset does not compensate for the current mesh origin",
    )
    pontoon_offsets = list(definition.get_editor_property("pontoon_offsets"))
    require(
        len(pontoon_offsets) == len(EXPECTED_PONTOON_OFFSETS),
        "Raft definition must contain four pontoon samples",
    )
    require(
        all(
            vector_nearly_equal(actual, expected)
            for actual, expected in zip(pontoon_offsets, EXPECTED_PONTOON_OFFSETS)
        ),
        "Raft pontoon samples do not match the current mesh footprint",
    )

    blueprint = load(BLUEPRINT_PATH)
    require(isinstance(blueprint, unreal.Blueprint), f"Not a Blueprint: {BLUEPRINT_PATH}")
    generated_class = require(
        blueprint.generated_class(), f"Blueprint has no generated class: {BLUEPRINT_PATH}"
    )
    defaults = unreal.get_default_object(generated_class)
    require(
        defaults.get_editor_property("raft_definition") == definition,
        "Raft Blueprint defaults do not reference DA_Raft_Default",
    )
    require(defaults.get_editor_property("replicates"), "Raft Actor must replicate")
    require(
        defaults.get_editor_property("replicate_movement"),
        "Raft Actor movement must replicate",
    )


def validate_experience():
    experience_blueprint = load(EXPERIENCE_PATH)
    experience_class = require(
        experience_blueprint.generated_class(),
        f"Experience has no generated class: {EXPERIENCE_PATH}",
    )
    experience = unreal.get_default_object(experience_class)
    enabled_features = list(
        experience.get_editor_property("game_features_to_enable")
    )
    require("Raft" in enabled_features, "Ocean Experience does not enable Raft")


def validate_test_map():
    load(TEST_MAP_PATH)
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    require(level_subsystem.load_level(TEST_MAP_PATH), f"Unable to load {TEST_MAP_PATH}")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    rafts = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor.get_actor_label() == TEST_ACTOR_LABEL
    ]
    require(
        len(rafts) == 1,
        f"Expected exactly one '{TEST_ACTOR_LABEL}', found {len(rafts)}",
    )
    require(
        isinstance(rafts[0], unreal.RaftActor),
        f"'{TEST_ACTOR_LABEL}' is not a RaftActor",
    )


def main():
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        ["/Raft", "/OceanAdventure"], True, True
    )
    validate_assets()
    validate_experience()
    validate_test_map()
    unreal.log("[RaftFeatureValidation] PASS")


if __name__ == "__main__":
    main()
