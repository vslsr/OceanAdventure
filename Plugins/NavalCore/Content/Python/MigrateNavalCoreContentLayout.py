"""Migrate NavalCore content into separate Blueprints and Arts roots.

The previous layout mixed Blueprint assets with meshes/materials and, after the authoring
scripts were run again, could contain two different assets with the same name:

    /NavalCore/Naval/...
    /NavalCore/NavalArts/...

The canonical layout is:

    /NavalCore/Blueprints/Cannon       Blueprint assets for the shared cannon
    /NavalCore/Blueprints/HelmWheel    Blueprint assets for the helm wheel, when present
    /NavalCore/Arts/Cannon             cannon meshes and materials
    /NavalCore/Arts/HelmWheel          helm-wheel meshes and materials

This must run inside Unreal Editor. In Output Log ``Cmd`` mode use:

    py "C:/EpicWkspc/OceanAdventure/Plugins/NavalCore/Content/Python/MigrateNavalCoreContentLayout.py"

The script is idempotent. It keeps an existing canonical destination first, otherwise it
prefers the asset already filed under NavalArts over the duplicate later regenerated under
Naval. Duplicate assets are consolidated through Unreal's asset API so their referencers are
repointed before the duplicate is deleted. Assets are never moved directly on disk. If an
interrupted migration left a redirector at a canonical destination, one run releases that
redirector and exits; restart Unreal and run the script again to reuse the package path safely.
"""

import unreal


PLUGIN_ROOT = "/NavalCore"
BLUEPRINT_ROOT = f"{PLUGIN_ROOT}/Blueprints"
ART_ROOT = f"{PLUGIN_ROOT}/Arts"

# NavalArts is preferred because it contains the assets deliberately moved into the grouped
# layout. Naval contains copies recreated later by scripts that still used the old path.
LEGACY_ROOTS = (
    f"{PLUGIN_ROOT}/NavalArts",
    f"{PLUGIN_ROOT}/Naval",
)

CONTENT_GROUPS = ("Cannon", "HelmWheel")

REQUIRED_DESTINATIONS = (
    f"{BLUEPRINT_ROOT}/Cannon/BP_Naval_Cannon",
    f"{BLUEPRINT_ROOT}/Cannon/BP_Naval_CannonballProjectile",
    f"{ART_ROOT}/Cannon/Meshes/SM_Naval_Cannon",
    f"{ART_ROOT}/Cannon/Meshes/SM_Naval_Cannonball",
    f"{ART_ROOT}/HelmWheel/SM_Naval_HelmWheel",
)


def log(message):
    unreal.log(f"[MigrateNavalCoreLayout] {message}")


def warn(message):
    unreal.log_warning(f"[MigrateNavalCoreLayout] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def package_of(asset):
    if asset is None:
        return ""
    return str(asset.get_path_name()).split(".", 1)[0]


def class_path_of(asset):
    if asset is None or asset.get_class() is None:
        return ""
    return str(asset.get_class().get_path_name())


def is_object_redirector(asset):
    return class_path_of(asset).endswith(".ObjectRedirector")


def class_name(asset_data):
    class_path = getattr(asset_data, "asset_class_path", None)
    if class_path is not None:
        return str(class_path.asset_name)
    return str(getattr(asset_data, "asset_class", ""))


def is_redirector(asset_data):
    return class_name(asset_data) == "ObjectRedirector"


def is_blueprint(asset_data):
    name = class_name(asset_data)
    package_name = str(asset_data.package_name)
    package_leaf = package_name.rpartition("/")[2]
    return package_leaf.startswith("BP_") or name == "Blueprint" or name.endswith("Blueprint")


def exact_asset(asset_path):
    """Return only a real asset at asset_path; an ObjectRedirector is never a valid target."""
    if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return None
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if package_of(asset) != asset_path or is_object_redirector(asset):
        return None
    return asset


def inferred_group(asset_name):
    if "Cannon" in asset_name:
        return "Cannon"
    if "HelmWheel" in asset_name or "Helm_Wheel" in asset_name:
        return "HelmWheel"
    return ""


def blueprint_destination(relative_path):
    parts = relative_path.split("/")
    asset_name = parts[-1]
    folders = parts[:-1]

    if folders and folders[0] in CONTENT_GROUPS:
        group = folders[0]
        subfolders = folders[1:]
    else:
        group = inferred_group(asset_name)
        subfolders = folders

    destination_parts = [BLUEPRINT_ROOT]
    if group:
        destination_parts.append(group)
    destination_parts.extend(subfolders)
    destination_parts.append(asset_name)
    return "/".join(destination_parts)


def art_destination(source_root, relative_path):
    parts = relative_path.split("/")
    asset_name = parts[-1]
    folders = parts[:-1]

    # NavalArts was already grouped by Cannon/HelmWheel, so its relative layout is retained.
    if source_root.endswith("/NavalArts"):
        return f"{ART_ROOT}/{relative_path}"

    if folders and folders[0] in CONTENT_GROUPS:
        return f"{ART_ROOT}/{relative_path}"

    # The older Naval layout put Cannon's Meshes and Materials directly below Naval.
    group = inferred_group(asset_name)
    if group:
        return "/".join([ART_ROOT, group, *folders, asset_name])
    return f"{ART_ROOT}/{relative_path}"


def destination_for(asset_data, source_root):
    source = str(asset_data.package_name)
    relative_path = source[len(source_root) + 1 :]
    if is_blueprint(asset_data):
        return blueprint_destination(relative_path)
    return art_destination(source_root, relative_path)


def collect_package_records(registry, source_root):
    """Collapse Blueprint/GeneratedClass AssetData rows into one package-level record."""
    records = {}
    if not unreal.EditorAssetLibrary.does_directory_exist(source_root):
        return records
    for asset_data in registry.get_assets_by_path(source_root, recursive=True):
        package_name = str(asset_data.package_name)
        if not package_name.startswith(f"{source_root}/"):
            continue
        record = records.setdefault(
            package_name,
            {"asset_data": asset_data, "has_real_asset": False, "is_blueprint": False},
        )
        if not is_redirector(asset_data):
            record["has_real_asset"] = True
            record["asset_data"] = asset_data
        record["is_blueprint"] = record["is_blueprint"] or is_blueprint(asset_data)
    return records


def package_is_redirector(registry, asset_path):
    package_root = asset_path.rpartition("/")[0]
    record = collect_package_records(registry, package_root).get(asset_path)
    return record is not None and not record["has_real_asset"]


def collect_plans(registry):
    plans = {}
    for source_root in LEGACY_ROOTS:
        for source, record in collect_package_records(registry, source_root).items():
            if not record["has_real_asset"]:
                continue
            asset_data = record["asset_data"]
            destination = destination_for(asset_data, source_root)
            sources = plans.setdefault(destination, [])
            if source not in sources:
                sources.append(source)

    # Repair an interrupted/older run that incorrectly filed BP_* packages under Arts.
    for source, record in collect_package_records(registry, ART_ROOT).items():
        if not record["has_real_asset"] or not record["is_blueprint"]:
            continue
        relative_path = source[len(ART_ROOT) + 1 :]
        destination = blueprint_destination(relative_path)
        sources = plans.setdefault(destination, [])
        if source not in sources:
            sources.append(source)
    return plans


def consolidate(canonical, duplicates):
    if not duplicates:
        return

    consolidate_assets = getattr(unreal.EditorAssetLibrary, "consolidate_assets", None)
    if consolidate_assets is None:
        subsystem_class = require(
            getattr(unreal, "EditorAssetSubsystem", None),
            "Neither EditorAssetLibrary.consolidate_assets nor EditorAssetSubsystem is available",
        )
        subsystem = require(
            unreal.get_editor_subsystem(subsystem_class),
            "Unable to obtain EditorAssetSubsystem",
        )
        consolidate_assets = subsystem.consolidate_assets

    duplicate_paths = [package_of(asset) for asset in duplicates]
    require(
        consolidate_assets(canonical, duplicates),
        f"Unable to consolidate {duplicate_paths} into {package_of(canonical)}",
    )
    log(f"Consolidated {', '.join(duplicate_paths)} into {package_of(canonical)}")


def preflight(plans):
    missing = []
    for destination in REQUIRED_DESTINATIONS:
        candidates = [destination, *plans.get(destination, [])]
        if not any(exact_asset(path) is not None for path in candidates):
            missing.append(destination)
    require(
        not missing,
        "Required NavalCore assets are missing before migration: " + ", ".join(missing),
    )

    # Detect a destination collision before the first mutation. Consolidation is valid only
    # for assets of the same UObject type.
    for destination, sources in plans.items():
        assets = [
            asset
            for path in (destination, *sources)
            if (asset := exact_asset(path)) is not None
        ]
        if not assets:
            continue
        canonical_class_path = class_path_of(assets[0])
        require(
            all(class_path_of(asset) == canonical_class_path for asset in assets[1:]),
            f"Type collision at {destination}; refusing to consolidate unlike assets",
        )


def plan_sort_key(item):
    destination, _ = item
    if destination.startswith(f"{ART_ROOT}/"):
        return (0, destination)
    if destination.endswith("BP_Naval_CannonballProjectile"):
        return (1, destination)
    return (2, destination)


def release_destination_redirectors(registry, plans):
    """Release target paths without reusing their packages in the same UE process."""
    occupied_destinations = sorted(
        destination
        for destination in plans
        if package_is_redirector(registry, destination)
    )
    for destination in occupied_destinations:
        require(
            unreal.EditorAssetLibrary.delete_asset(destination),
            f"Unable to remove destination redirector {destination}",
        )
        log(f"Released destination redirector {destination}")
    return occupied_destinations


def execute_plan(destination, sources):
    canonical = exact_asset(destination)
    if canonical is None:
        # collect_plans visits NavalArts before Naval, preserving the intended source priority.
        for source in sources:
            canonical = exact_asset(source)
            if canonical is not None:
                break
    canonical = require(canonical, f"No source asset is available for {destination}")

    duplicates = []
    for source in sources:
        candidate = exact_asset(source)
        if candidate is not None and package_of(candidate) != package_of(canonical):
            duplicates.append(candidate)
    mutated = bool(duplicates)
    consolidate(canonical, duplicates)

    canonical_path = package_of(canonical)
    if canonical_path != destination:
        require(
            not unreal.EditorAssetLibrary.does_asset_exist(destination),
            f"A non-migratable object already occupies {destination}",
        )
        require(
            unreal.EditorAssetLibrary.rename_asset(canonical_path, destination),
            f"Unable to move {canonical_path} -> {destination}",
        )
        mutated = True
        log(f"Moved {canonical_path} -> {destination}")

    landed = require(exact_asset(destination), f"Migration did not retain {destination}")
    if mutated:
        require(
            unreal.EditorAssetLibrary.save_loaded_asset(landed, only_if_is_dirty=False),
            f"Unable to save {destination}",
        )


def report_legacy_redirectors(registry):
    """Report redirectors without loading, querying or force-deleting them."""
    registry.scan_paths_synchronous([PLUGIN_ROOT], True, True)
    redirectors = set()
    for source_root in LEGACY_ROOTS:
        for asset_data in registry.get_assets_by_path(source_root, recursive=True):
            if is_redirector(asset_data):
                redirectors.add(str(asset_data.package_name))

    for redirector_path in sorted(redirectors):
        warn(
            f"Redirector remains at {redirector_path}. Use Fix Up Redirectors in Folder on "
            "/NavalCore after saving its referencers."
        )
    return sorted(redirectors)


def cleanup_empty_legacy_directories():
    for source_root in reversed(LEGACY_ROOTS):
        if not unreal.EditorAssetLibrary.does_directory_exist(source_root):
            continue
        remaining = unreal.EditorAssetLibrary.list_assets(
            source_root, recursive=True, include_folder=False
        )
        if not remaining and unreal.EditorAssetLibrary.delete_directory(source_root):
            log(f"Removed empty legacy directory {source_root}")


def verify(registry):
    registry.scan_paths_synchronous([PLUGIN_ROOT], True, True)
    problems = []
    for destination in REQUIRED_DESTINATIONS:
        if exact_asset(destination) is None:
            problems.append(f"Missing {destination}")

    for source_root in LEGACY_ROOTS:
        for source, record in collect_package_records(registry, source_root).items():
            if record["has_real_asset"]:
                problems.append(f"Legacy asset remains: {source}")

    for source, record in collect_package_records(registry, ART_ROOT).items():
        if record["has_real_asset"] and record["is_blueprint"]:
            problems.append(f"Blueprint remains under Arts: {source}")

    if problems:
        for problem in problems:
            warn(problem)
        raise RuntimeError("NavalCore layout verification failed")


def main():
    require(
        unreal.EditorAssetLibrary.does_directory_exist(PLUGIN_ROOT),
        f"{PLUGIN_ROOT} is not mounted. Enable content in NavalCore.uplugin and restart.",
    )
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([PLUGIN_ROOT], True, True)

    plans = collect_plans(registry)
    preflight(plans)

    released_redirectors = release_destination_redirectors(registry, plans)
    if released_redirectors:
        warn(
            "NAVALCORE_LAYOUT_MIGRATION_RESTART_REQUIRED: released canonical destination "
            "redirectors; restart Unreal and run this script again before the package paths "
            "are reused."
        )
        return

    for destination, sources in sorted(plans.items(), key=plan_sort_key):
        execute_plan(destination, sources)

    remaining_redirectors = report_legacy_redirectors(registry)
    cleanup_empty_legacy_directories()
    verify(registry)

    if remaining_redirectors:
        warn(
            "NAVALCORE_LAYOUT_MIGRATION_OK_WITH_REDIRECTORS: canonical assets are valid; "
            "save the reported referencers and fix redirectors, then run this script again."
        )
    else:
        warn("NAVALCORE_LAYOUT_MIGRATION_OK: Blueprints and Arts are canonical.")


if __name__ == "__main__":
    main()
