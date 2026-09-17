"""Build /OceanAdventure/Maps/L_LineArtPreview -- a level that shows the line-art style.

The materials alone do not produce the look. A default level fights it from three
directions at once: a sky and atmosphere tint the background, the ACES tone curve crushes
the pale paper and lifts pure black to grey, and auto exposure makes the ink drift as the
camera moves. This level removes all three and supplies the paper to draw on.

Why this script lives in the OceanAdventure GameFeature and not in LineArtCore: the map
points its World Settings at BP_Experience_Ocean, and per AGENTS.md a general plugin's
content may only reference Engine and other general plugins. A level that names a
GameFeature's Experience belongs to that GameFeature.

Run CreateLineArtCoreAssets.py first -- this script consumes the materials it authors and
stops with a clear message if they are missing.

Run in the Output Log's **Python input mode**, NOT Cmd mode -- the mode dropdown sits to
the LEFT of the input box. Cmd mode only accepts engine console commands; it discards a
bare ``import`` silently, so the script appears to run and do nothing. The only Python
entry allowed in Cmd mode is ``py "<script>"``.

Run in the full Unreal Editor (NOT in PIE); the GameFeature's Content/Python is already on
sys.path, so no path is needed:

    import importlib, CreateLineArtPreviewLevel
    importlib.reload(CreateLineArtPreviewLevel)
    CreateLineArtPreviewLevel.main()
"""

import unreal


MAP_PATH = "/OceanAdventure/Maps/L_LineArtPreview"
FEATURE_ROOT = "/OceanAdventure"
EXPERIENCE_PATH = "/OceanAdventure/Experience/BP_Experience_Ocean"

LINE_ART_ROOT = "/LineArtCore/Materials"
PAPER_MATERIAL_PATH = f"{LINE_ART_ROOT}/MI_LineArt_Paper"

SPHERE_MESH_PATH = "/Engine/BasicShapes/Sphere.Sphere"
CYLINDER_MESH_PATH = "/Engine/BasicShapes/Cylinder.Cylinder"

PREVIEW_ACTOR_CLASS_PATH = "/Script/LineArtCoreRuntime.LineArtPreviewActor"

#: Every actor this script owns carries this prefix, so a re-run replaces its own work and
#: leaves anything placed by hand alone.
LABEL_PREFIX = "LineArtPreview_"

#: Big enough to sit outside anything the preview shows, small enough to stay inside the
#: camera's far plane. The engine sphere is 100cm across, so scale is the radius in metres.
BACKDROP_SCALE = 300.0


def log(message):
    unreal.log(f"[LineArtPreviewLevel] {message}")


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
        "Cannot build the preview level while Play/PIE is active. Click Stop, then re-run.",
    )
    return subsystem


def load_class(path):
    return require(unreal.load_class(None, path), f"Unable to load class: {path}")


def load_asset(path):
    return require(unreal.EditorAssetLibrary.load_asset(path), f"Unable to load asset: {path}")


def load_line_art_asset(path):
    require(
        unreal.EditorAssetLibrary.does_asset_exist(path),
        f"Missing {path}; run CreateLineArtCoreAssets.py first.",
    )
    return load_asset(path)


def load_or_create_map(level_subsystem):
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    current_world = editor_subsystem.get_editor_world()
    current_map_path = current_world.get_path_name().split(".", 1)[0] if current_world else None

    if current_map_path == MAP_PATH:
        return

    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        require(level_subsystem.load_level(MAP_PATH), f"Unable to load {MAP_PATH}")
    else:
        # new_level gives an empty level: no sky, no atmosphere, no directional light. That
        # is the starting point this style needs, not a shortcut -- the fills are Unlit and
        # take nothing from those actors, while the sky would tint the backdrop blue.
        require(level_subsystem.new_level(MAP_PATH), f"Unable to create {MAP_PATH}")


def remove_previous_generated_actors(actor_subsystem):
    """Only this script's own actors, so anything placed by hand survives a re-run."""
    generated = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor.get_actor_label().startswith(LABEL_PREFIX)
    ]
    if generated:
        actor_subsystem.destroy_actors(generated)
    return len(generated)


def spawn(actor_subsystem, actor_class, label, location, rotation=None):
    actor = require(
        actor_subsystem.spawn_actor_from_class(actor_class, location, rotation or unreal.Rotator()),
        f"Unable to spawn {label}",
    )
    actor.set_actor_label(f"{LABEL_PREFIX}{label}")
    return actor


def build_backdrop(actor_subsystem):
    """An inside-out sphere of flat paper colour.

    Not a Sky Atmosphere and not a Sky Light: both would light nothing (the fills are Unlit)
    while colouring the background, which is the opposite of what is wanted.
    """
    backdrop = spawn(
        actor_subsystem,
        load_class("/Script/Engine.StaticMeshActor"),
        "Backdrop",
        unreal.Vector(0.0, 0.0, 0.0),
    )
    backdrop.set_actor_scale3d(unreal.Vector(BACKDROP_SCALE, BACKDROP_SCALE, BACKDROP_SCALE))

    mesh_component = require(
        backdrop.get_component_by_class(load_class("/Script/Engine.StaticMeshComponent")),
        "StaticMeshActor has no StaticMeshComponent",
    )
    mesh_component.set_static_mesh(load_asset(SPHERE_MESH_PATH))
    mesh_component.set_material(0, load_line_art_asset(PAPER_MATERIAL_PATH))
    # The camera lives inside it, so it must neither block movement nor be hit by traces.
    mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    mesh_component.set_cast_shadow(False)
    return backdrop


def set_post_process_value(settings, name, value, critical):
    """Set one PostProcessSettings field plus its override flag.

    Every field in this struct is inert until its ``override_`` twin is true, and a missed
    override is silent: the value sits in the asset and the volume ignores it.

    ``critical`` separates the settings that define the look from the ones that only tidy
    it. Tone curve and exposure are the whole reason this volume exists; bloom is a nicety,
    and losing it to a property rename should not cost the level.
    """
    try:
        settings.set_editor_property(f"override_{name}", True)
        settings.set_editor_property(name, value)
        return True
    except Exception as error:
        message = (
            f"PostProcessSettings.{name} could not be set: {error}. "
            f"Check the UE 5.7 Python exposure for this property."
        )
        if critical:
            raise RuntimeError(message) from error
        unreal.log_warning(f"[LineArtPreviewLevel] {message}")
        return False


def build_post_process(actor_subsystem):
    """The volume that stops the engine from 'improving' the picture.

    The tone curve is the one that matters most. ACES crushes the pale paper and lifts pure
    black to grey -- exactly what the character ink layer opts out of in the reference
    implementation -- so a line-art scene graded through it never reads as ink on paper.
    Auto exposure is the same problem in motion: the ink drifts as the camera turns.
    """
    volume = spawn(
        actor_subsystem,
        load_class("/Script/Engine.PostProcessVolume"),
        "PostProcess",
        unreal.Vector(0.0, 0.0, 0.0),
    )
    volume.set_editor_property("unbound", True)

    settings = volume.get_editor_property("settings")
    set_post_process_value(settings, "tone_curve_amount", 0.0, critical=True)
    set_post_process_value(settings, "expand_gamut", 0.0, critical=True)
    set_post_process_value(
        settings, "auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL, critical=True
    )
    # Min == Max is what actually pins the exposure; Manual metering alone still adapts.
    set_post_process_value(settings, "auto_exposure_min_brightness", 1.0, critical=True)
    set_post_process_value(settings, "auto_exposure_max_brightness", 1.0, critical=True)
    set_post_process_value(settings, "bloom_intensity", 0.0, critical=False)
    set_post_process_value(settings, "motion_blur_amount", 0.0, critical=False)
    set_post_process_value(settings, "vignette_intensity", 0.0, critical=False)
    volume.set_editor_property("settings", settings)
    return volume


def build_preview_actors(actor_subsystem):
    """Three subjects: two that work, one that shows the gap that is still open.

    The spheres are the target. The cylinder is deliberate: its flat caps meet its side at a
    hard crease, and an inverted hull extruded along hard-edged normals has nowhere to go
    at a crease, so the outline splits open there. That is not a bug in the material -- it
    is the averaged-normal bake that production meshes still need, made visible.
    """
    preview_class = load_class(PREVIEW_ACTOR_CLASS_PATH)
    sphere = load_asset(SPHERE_MESH_PATH)
    cylinder = load_asset(CYLINDER_MESH_PATH)

    subjects = [
        ("SmoothSphere", unreal.Vector(0.0, 0.0, 100.0), 1.6, sphere,
         unreal.LinearColor(0.82, 0.78, 0.70, 1.0)),
        ("SmoothSphereWarm", unreal.Vector(-320.0, 180.0, 70.0), 1.1, sphere,
         unreal.LinearColor(0.74, 0.62, 0.45, 1.0)),
        ("HardEdgeCylinder", unreal.Vector(320.0, -160.0, 90.0), 1.3, cylinder,
         unreal.LinearColor(0.68, 0.74, 0.66, 1.0)),
    ]

    spawned = []
    for label, location, scale, mesh, color in subjects:
        actor = spawn(actor_subsystem, preview_class, label, location)
        actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        actor.set_editor_property("preview_mesh", mesh)
        actor.set_editor_property("fill_color", color)
        # set_editor_property already routes through PostEditChangeProperty, which
        # rebuilds the preview. Calling it again is belt-and-braces, and it must not
        # take the level down if the exposed name ever differs.
        rebuild = getattr(actor, "rebuild_preview", None)
        if rebuild is not None:
            rebuild()
        spawned.append(actor)
    return spawned


def find_world_settings(world):
    """Reach AWorldSettings through whatever this engine version actually exposes.

    GameplayStatics.get_world_settings does NOT exist in UE 5.7's Python bindings, despite
    two other scripts in this repository calling it -- neither had ever run past that line.
    "The repo already writes it this way" is a lead, not evidence that an API exists.
    """
    getter = getattr(unreal.GameplayStatics, "get_world_settings", None)
    if getter is not None:
        return getter(world)

    getter = getattr(world, "get_world_settings", None)
    if getter is not None:
        return getter()

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in actor_subsystem.get_all_level_actors():
        if isinstance(actor, unreal.WorldSettings):
            return actor

    return None


def set_map_default_experience(level_subsystem):
    """Point World Settings at the Ocean Adventure Experience.

    Without this the map falls through to ULyraFrameworkSettings::DefaultExperienceId and
    PIE opens the frontend menu instead of the level -- the classic 'I set up a map and it
    still shows the menu' symptom.
    """
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = require(editor_subsystem.get_editor_world(), "No editor world after loading the map")
    world_settings = find_world_settings(world)
    if world_settings is None:
        unreal.log_warning(
            f"[LineArtPreviewLevel] Could not reach the World Settings of {MAP_PATH} from Python. "
            "Open the map, use Window > World Settings, and set Default Gameplay Experience to "
            "BP_Experience_Ocean by hand; everything else in this level is already built."
        )
        return None
    if not isinstance(world_settings, unreal.LyraWorldSettings):
        unreal.log_warning(
            f"[LineArtPreviewLevel] {MAP_PATH} uses {type(world_settings).__name__}, not "
            "LyraWorldSettings, so it has no DefaultGameplayExperience. Set the World "
            "Settings class in Project Settings > Engine > General Settings, then re-run."
        )
        return None

    experience_class = require(
        unreal.EditorAssetLibrary.load_blueprint_class(EXPERIENCE_PATH),
        f"Unable to load blueprint class: {EXPERIENCE_PATH}",
    )
    # DefaultGameplayExperience is EditDefaultsOnly, and the WorldSettings in a level is an
    # INSTANCE, so Python cannot write it (PY-LYRA-002). Try anyway -- a future engine or a
    # native bridge may allow it -- but treat failure as the expected path and hand the user
    # the two clicks rather than pretending the level is finished.
    try:
        world_settings.set_editor_property("default_gameplay_experience", experience_class)
    except Exception as error:
        unreal.log_warning(
            f"[LineArtPreviewLevel] Could not set DefaultGameplayExperience from Python: {error}\n"
            "  This is expected: the property is EditDefaultsOnly and a level's WorldSettings "
            "is an instance.\n"
            "  FINISH BY HAND: open the map, Window > World Settings > Default Gameplay "
            "Experience, choose BP_Experience_Ocean, save.\n"
            "  Everything else in this level is already built. Without this step PIE opens the "
            "frontend menu instead of the level."
        )
        return None

    stored = world_settings.get_editor_property("default_gameplay_experience")
    require(
        stored is not None and stored.get_path_name() == experience_class.get_path_name(),
        f"World Settings did not retain the experience; it reads back as {stored}",
    )
    log(f"{MAP_PATH}: DefaultGameplayExperience = BP_Experience_Ocean")
    return experience_class


def main():
    level_subsystem = require_editor_asset_mode()

    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [FEATURE_ROOT, LINE_ART_ROOT], True, False
    )

    load_or_create_map(level_subsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    removed = remove_previous_generated_actors(actor_subsystem)
    if removed:
        log(f"Replaced {removed} actor(s) from a previous run")

    build_backdrop(actor_subsystem)
    build_post_process(actor_subsystem)
    spawn(actor_subsystem, load_class("/Script/Engine.PlayerStart"), "PlayerStart",
          unreal.Vector(-700.0, 0.0, 120.0))
    build_preview_actors(actor_subsystem)

    experience = set_map_default_experience(level_subsystem)
    require(level_subsystem.save_current_level(), f"Unable to save {MAP_PATH}")

    log(f"Built {MAP_PATH}")
    if experience is None:
        log("LINEART_PREVIEW_LEVEL_OK_EXPERIENCE_PENDING")
        log("  The level is built but its Experience is NOT set. See the warning above.")
    else:
        log("LINEART_PREVIEW_LEVEL_OK")


if __name__ == "__main__":
    main()
