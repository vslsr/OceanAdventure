"""Restore the shared cannon mesh's six material assignments by stable slot name.

The mesh keeps its imported material slot names even when every MaterialInterface reference
is accidentally replaced by one material.  This repair therefore does not depend on slot
indices: it validates the complete slot-name set first, loads every required material before
mutating the mesh, then assigns and reads back each slot.  Safe to run repeatedly.

Run in Unreal Editor Output Log Cmd mode:

    py "C:/EpicWkspc/OceanAdventure/Plugins/NavalCore/Content/Python/RepairNavalCannonMaterials.py"
"""

import unreal


MESH_PATH = "/NavalCore/Arts/Cannon/Meshes/SM_Naval_Cannon"
MATERIAL_ROOT = "/NavalCore/Arts/Cannon/Materials"

SLOT_MATERIAL_PATHS = {
    "M_Cannon_Wood": f"{MATERIAL_ROOT}/M_Cannon_Wood",
    "M_Cannon_DarkWood": f"{MATERIAL_ROOT}/M_Cannon_DarkWood",
    "M_Cannon_DarkMetal": f"{MATERIAL_ROOT}/M_Cannon_DarkMetal",
    "M_Cannon_WheelRim": f"{MATERIAL_ROOT}/M_Cannon_WheelRim",
    "M_Cannon_Bronze": f"{MATERIAL_ROOT}/M_Cannon_Bronze",
    "M_Cannon_Bore": f"{MATERIAL_ROOT}/M_Cannon_Bore",
}


def log(message):
    unreal.log(f"[RepairNavalCannonMaterials] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def require_editor_asset_mode():
    """Block content-asset edits while PIE owns runtime references to those assets."""
    subsystem = require(
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem),
        "LevelEditorSubsystem is unavailable; run this script in the full Unreal Editor",
    )
    require(
        not subsystem.is_in_play_in_editor(),
        "Cannot repair cannon materials while Play/PIE is active. Click Stop, then run this script again.",
    )


def package_path(asset):
    if asset is None:
        return ""
    return str(asset.get_path_name()).split(".", 1)[0]


def load_asset(asset_path):
    require(
        unreal.EditorAssetLibrary.does_asset_exist(asset_path),
        f"Required asset does not exist: {asset_path}",
    )
    return require(
        unreal.EditorAssetLibrary.load_asset(asset_path),
        f"Unable to load required asset: {asset_path}",
    )


def static_material_entries(mesh):
    getter = getattr(mesh, "get_static_materials", None)
    return list(getter() if getter is not None else mesh.get_editor_property("static_materials"))


def resolve_slot_name(entry, index):
    material_slot_name = str(entry.get_editor_property("material_slot_name"))
    imported_slot_name = str(entry.get_editor_property("imported_material_slot_name"))
    recognized_names = {
        name
        for name in (material_slot_name, imported_slot_name)
        if name in SLOT_MATERIAL_PATHS
    }
    require(
        len(recognized_names) == 1,
        (
            f"Material slot {index} is not an unambiguous cannon slot: "
            f"MaterialSlotName={material_slot_name}, "
            f"ImportedMaterialSlotName={imported_slot_name}"
        ),
    )
    return next(iter(recognized_names))


def validated_slots(mesh):
    entries = static_material_entries(mesh)
    require(
        len(entries) == len(SLOT_MATERIAL_PATHS),
        (
            f"{MESH_PATH} must have {len(SLOT_MATERIAL_PATHS)} material slots; "
            f"found {len(entries)}"
        ),
    )

    slots = [resolve_slot_name(entry, index) for index, entry in enumerate(entries)]
    duplicates = sorted(name for name in set(slots) if slots.count(name) != 1)
    missing = sorted(set(SLOT_MATERIAL_PATHS) - set(slots))
    unexpected = sorted(set(slots) - set(SLOT_MATERIAL_PATHS))
    require(
        not duplicates and not missing and not unexpected,
        (
            f"Unexpected material-slot layout on {MESH_PATH}: "
            f"duplicates={duplicates}, missing={missing}, unexpected={unexpected}"
        ),
    )
    return slots


def load_materials():
    materials = {}
    for slot_name, material_path in SLOT_MATERIAL_PATHS.items():
        material = load_asset(material_path)
        require(
            isinstance(material, unreal.MaterialInterface),
            (
                f"{material_path} must derive from MaterialInterface; "
                f"got {material.get_class().get_name()}"
            ),
        )
        materials[slot_name] = material
    return materials


def verify_assignments(mesh, slots):
    for index, slot_name in enumerate(slots):
        expected_path = SLOT_MATERIAL_PATHS[slot_name]
        configured_path = package_path(mesh.get_material(index))
        require(
            configured_path == expected_path,
            (
                f"{MESH_PATH} slot {index} ({slot_name}) did not retain {expected_path}; "
                f"got {configured_path or '<None>'}"
            ),
        )


def main():
    require_editor_asset_mode()
    mesh = load_asset(MESH_PATH)
    require(
        isinstance(mesh, unreal.StaticMesh),
        f"{MESH_PATH} must be a StaticMesh; got {mesh.get_class().get_name()}",
    )

    # Validate the complete slot layout and all dependencies before changing the mesh, so a
    # missing/renamed material cannot leave a partially repaired asset in memory.
    slots = validated_slots(mesh)
    materials = load_materials()

    changed_slots = 0
    for index, slot_name in enumerate(slots):
        expected_material = materials[slot_name]
        if package_path(mesh.get_material(index)) == package_path(expected_material):
            continue
        mesh.set_material(index, expected_material)
        changed_slots += 1
        log(f"Assigned slot {index} ({slot_name}) -> {package_path(expected_material)}")

    verify_assignments(mesh, slots)
    if changed_slots:
        require(
            unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=True),
            f"Unable to save repaired mesh: {MESH_PATH}",
        )

    # Read again after saving (or on an idempotent no-op run) and assert the stable result.
    configured_slots = validated_slots(mesh)
    require(configured_slots == slots, f"{MESH_PATH} material-slot order changed unexpectedly")
    verify_assignments(mesh, configured_slots)
    log(
        "NAVAL_CANNON_MATERIALS_REPAIRED "
        f"changed_slots={changed_slots} slots={len(configured_slots)}"
    )


if __name__ == "__main__":
    main()
