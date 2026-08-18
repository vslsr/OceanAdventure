import unreal


ASSET_NAME = "OceanAdventure"
PACKAGE_PATH = "/OceanAdventure"
ASSET_PATH = f"{PACKAGE_PATH}/{ASSET_NAME}"


if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
    unreal.log(f"GameFeatureData already exists: {ASSET_PATH}")
else:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.GameFeatureData)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset = asset_tools.create_asset(
        ASSET_NAME,
        PACKAGE_PATH,
        unreal.GameFeatureData,
        factory,
    )
    if asset is None:
        raise RuntimeError(f"Failed to create GameFeatureData: {ASSET_PATH}")

    if not unreal.EditorAssetLibrary.save_asset(ASSET_PATH, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save GameFeatureData: {ASSET_PATH}")

    unreal.log(f"Created GameFeatureData: {ASSET_PATH}")
