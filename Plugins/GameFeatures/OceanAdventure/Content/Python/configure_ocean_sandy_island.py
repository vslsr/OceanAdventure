"""Create or update an OceanGenerationSettings asset with a sandy-island preset.

Usage in the Unreal Editor:

1. Optionally select one OceanGenerationSettings data asset in the Content Browser.
2. Run this file with `py` from the Output Log, or import it from the Python console:

       import configure_ocean_sandy_island

   To run it again in the same editor session:

       import importlib, configure_ocean_sandy_island
       importlib.reload(configure_ocean_sandy_island)

The selected asset must belong to the OceanAdventure GameFeature. If no compatible
asset is selected, the script updates or creates
`/OceanAdventure/Settings/DA_OceanGenerationSettings`.
"""

import unreal


GAME_FEATURE_CONTENT_ROOT = "/OceanAdventure"
DEFAULT_SETTINGS_PATH = (
    f"{GAME_FEATURE_CONTENT_ROOT}/Settings/DA_OceanGenerationSettings"
)

# Unreal distances are centimeters. Only terrain-profile and beach-appearance fields
# are changed; world seed, island placement, size, and density remain untouched.
SANDY_ISLAND_PRESET = {
    "water_height": 0.0,
    "island_peak_height": 350.0,
    "beach_slope_angle_degrees": 5.0,
    "island_plateau_radius_ratio": 0.20,
    "terrain_height_amplitude": 100.0,
    "beach_band_height": 350.0,
}


def log(message):
    unreal.log(f"[OceanSandyIsland] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def get_settings_class():
    settings_class = getattr(unreal, "OceanGenerationSettings", None)
    if settings_class is None:
        raise RuntimeError(
            "unreal.OceanGenerationSettings is unavailable. Enable and compile the "
            "OceanCore plugin, then restart the editor."
        )
    return settings_class


def get_asset_package_path(asset):
    return asset.get_path_name().partition(".")[0]


def require_game_feature_ownership(asset):
    package_path = get_asset_package_path(asset)
    expected_prefix = f"{GAME_FEATURE_CONTENT_ROOT}/"
    if not package_path.startswith(expected_prefix):
        raise RuntimeError(
            "OceanAdventure settings must stay inside the OceanAdventure GameFeature. "
            f"Move or recreate {package_path} below {GAME_FEATURE_CONTENT_ROOT} before "
            "running this script."
        )
    return asset


def get_selected_settings(settings_class):
    selected_assets = unreal.EditorUtilityLibrary.get_selected_assets()
    selected_settings = [
        asset for asset in selected_assets if isinstance(asset, settings_class)
    ]

    if len(selected_settings) > 1:
        paths = ", ".join(asset.get_path_name() for asset in selected_settings)
        raise RuntimeError(
            "Select at most one OceanGenerationSettings asset before running the script. "
            f"Selected: {paths}"
        )

    if not selected_settings:
        return None

    return require_game_feature_ownership(selected_settings[0])


def load_or_create_default_settings(settings_class):
    if unreal.EditorAssetLibrary.does_asset_exist(DEFAULT_SETTINGS_PATH):
        existing = require(
            unreal.EditorAssetLibrary.load_asset(DEFAULT_SETTINGS_PATH),
            f"Failed to load existing settings asset: {DEFAULT_SETTINGS_PATH}",
        )
        if not isinstance(existing, settings_class):
            raise RuntimeError(
                f"{DEFAULT_SETTINGS_PATH} exists as {type(existing).__name__}, but an "
                "OceanGenerationSettings asset is required."
            )
        log(f"Using existing default asset: {DEFAULT_SETTINGS_PATH}")
        return require_game_feature_ownership(existing)

    package_path, _, asset_name = DEFAULT_SETTINGS_PATH.rpartition("/")
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", settings_class)
    settings = require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, settings_class, factory
        ),
        f"Failed to create settings asset: {DEFAULT_SETTINGS_PATH}",
    )
    log(f"Created settings asset: {DEFAULT_SETTINGS_PATH}")
    return require_game_feature_ownership(settings)


def resolve_settings_asset(settings_class):
    selected = get_selected_settings(settings_class)
    if selected is not None:
        log(f"Using selected asset: {selected.get_path_name()}")
        return selected
    return load_or_create_default_settings(settings_class)


def apply_preset(settings):
    with unreal.ScopedEditorTransaction("Apply Ocean Sandy Island Preset"):
        for property_name, value in SANDY_ISLAND_PRESET.items():
            settings.set_editor_property(property_name, value)


def validate(settings):
    require_game_feature_ownership(settings)

    water_height = float(settings.get_editor_property("water_height"))
    peak_height = float(settings.get_editor_property("island_peak_height"))
    ocean_floor_height = float(settings.get_editor_property("ocean_floor_height"))
    slope_angle = float(settings.get_editor_property("beach_slope_angle_degrees"))
    plateau_ratio = float(settings.get_editor_property("island_plateau_radius_ratio"))
    beach_band_height = float(settings.get_editor_property("beach_band_height"))

    if peak_height <= water_height:
        raise RuntimeError("Island Peak Height must be above Water Height.")
    if ocean_floor_height >= water_height:
        raise RuntimeError("Ocean Floor Height must be below Water Height.")
    if not 5.0 <= slope_angle <= 40.0:
        raise RuntimeError("Beach Slope Angle Degrees must be between 5 and 40.")
    if not 0.0 <= plateau_ratio <= 0.95:
        raise RuntimeError("Island Plateau Radius Ratio must be between 0 and 0.95.")
    if beach_band_height < 0.0:
        raise RuntimeError("Beach Band Height cannot be negative.")

    for property_name, expected in SANDY_ISLAND_PRESET.items():
        actual = float(settings.get_editor_property(property_name))
        if abs(actual - expected) > 0.001:
            raise RuntimeError(
                f"Failed to persist {property_name}: expected {expected}, got {actual}."
            )


def main():
    settings_class = get_settings_class()
    settings = resolve_settings_asset(settings_class)
    apply_preset(settings)
    validate(settings)

    require(
        unreal.EditorAssetLibrary.save_loaded_asset(settings, only_if_is_dirty=False),
        f"Failed to save settings asset: {settings.get_path_name()}",
    )

    log(f"Saved sandy-island preset to {settings.get_path_name()}")
    log(
        "Peak=350 cm, Slope=5 deg, PlateauRatio=0.20, Roughness=100 cm, "
        "BeachBand=350 cm. Reselect this asset in Ocean Core Island Debugger to refresh."
    )


main()
