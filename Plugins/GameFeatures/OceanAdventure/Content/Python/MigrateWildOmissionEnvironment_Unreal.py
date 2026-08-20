"""Unreal-side worker for MigrateWildOmissionEnvironment.py."""

import os
import shutil

import unreal


SOURCE_PROJECT_ROOT = os.environ.get(
    "WILD_OMISSION_ROOT", r"E:\WildOmission-1.0.1-beta"
)
DESTINATION_ROOT = "/OceanAdventure/Environment/WildOmission"
SOURCE_PACKAGES = [
    "/Game/WildOmissionCore/Art/Common/T_Fingerprints_01",
    "/Game/WildOmissionCore/Art/Common/T_Fingerprints_02",
    "/Game/WildOmissionCore/Art/Common/T_FoliageNoise",
    "/Game/WildOmissionCore/Art/Common/T_Metal_Scratched_01",
    "/Game/WildOmissionCore/Art/Common/T_Metal_Scratched_02",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_DistanceFieldMask",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_GreenColor",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_PlasticSpecular",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_ReactiveRipple",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_WaterWave",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_WhiteColor",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_WorldPositionShading",
    "/Game/WildOmissionCore/Art/Environment/M_Water",
    "/Game/WorldGeneration/M_Terrain",
    "/Game/WorldGeneration/SM_Water",
]
PACKAGE_EXTENSIONS = (".uasset", ".uexp", ".ubulk", ".uptnl")


def log(message):
    unreal.log(f"[WildOmissionMigration] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def destination_package(source_package):
    return DESTINATION_ROOT + source_package.removeprefix("/Game")


def file_base(root, source_package):
    relative = source_package.removeprefix("/Game/").replace("/", os.sep)
    return os.path.join(root, relative)


def copy_source_packages():
    project_content = unreal.Paths.project_content_dir()
    source_content = os.path.join(SOURCE_PROJECT_ROOT, "Content")
    for package in SOURCE_PACKAGES:
        copied = False
        source_base = file_base(source_content, package)
        destination_base = file_base(project_content, package)
        for extension in PACKAGE_EXTENSIONS:
            source = source_base + extension
            if not os.path.isfile(source):
                continue
            destination = destination_base + extension
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            shutil.copy2(source, destination)
            copied = True
        require(copied, f"Missing source package: {package}")


def dependency_options():
    return unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True,
        include_hard_package_references=True,
        include_searchable_names=False,
        include_soft_management_references=False,
        include_hard_management_references=False,
    )


def validate_destination_assets(load_assets):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([DESTINATION_ROOT], True, False)
    stale_roots = ("/Game/WildOmissionCore", "/Game/WorldGeneration")

    for source_package in SOURCE_PACKAGES:
        destination = destination_package(source_package)
        require(
            unreal.EditorAssetLibrary.does_asset_exist(destination),
            f"Migrated asset is missing: {destination}",
        )
        if load_assets:
            require(
                unreal.EditorAssetLibrary.load_asset(destination),
                f"Unable to load migrated asset: {destination}",
            )
        dependencies = [
            str(item)
            for item in registry.get_dependencies(destination, dependency_options())
        ]
        stale = [item for item in dependencies if item.startswith(stale_roots)]
        require(not stale, f"{destination} still references staging assets: {stale}")


def prepare():
    engine_version = unreal.SystemLibrary.get_engine_version()
    require(engine_version.startswith("5.3"), f"Prepare must run in UE 5.3, got {engine_version}")
    copy_source_packages()

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(
        ["/Game/WildOmissionCore", "/Game/WorldGeneration"], True, False
    )
    loaded_assets = [
        require(
            unreal.EditorAssetLibrary.load_asset(package),
            f"Unable to load source package: {package}",
        )
        for package in SOURCE_PACKAGES
    ]
    require(len(loaded_assets) == len(SOURCE_PACKAGES), "Incomplete source asset set")

    for source_package in SOURCE_PACKAGES:
        destination = destination_package(source_package)
        require(
            unreal.EditorAssetLibrary.rename_asset(source_package, destination),
            f"Unable to move {source_package} to {destination}",
        )
        log(f"Prepared {source_package} -> {destination}")

    for source_package in SOURCE_PACKAGES:
        destination = destination_package(source_package)
        require(
            unreal.EditorAssetLibrary.save_asset(destination, only_if_is_dirty=False),
            f"Unable to save migrated asset: {destination}",
        )

    validate_destination_assets(load_assets=False)
    for source_package in SOURCE_PACKAGES:
        if unreal.EditorAssetLibrary.does_asset_exist(source_package):
            require(
                unreal.EditorAssetLibrary.delete_asset(source_package),
                f"Unable to remove staging redirector: {source_package}",
            )
    log(f"Prepared and validated {len(SOURCE_PACKAGES)} assets in UE 5.3")


def validate():
    engine_version = unreal.SystemLibrary.get_engine_version()
    require(engine_version.startswith("5.7"), f"Validate must run in UE 5.7, got {engine_version}")
    validate_destination_assets(load_assets=True)
    log(f"Loaded and validated {len(SOURCE_PACKAGES)} assets in UE 5.7")


mode = os.environ.get("WILD_OMISSION_MIGRATION_MODE", "")
if mode == "prepare":
    prepare()
elif mode == "validate":
    validate()
else:
    raise RuntimeError(f"Unknown WILD_OMISSION_MIGRATION_MODE: {mode!r}")
