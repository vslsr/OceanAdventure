"""Port the SkyLand player-control feel onto the Ocean Adventure pawn.

SkyLand drives its player from a declarative scheme (``config/input/player.input.json``)
plus one shared movement simulation (``shared/physics/stepCharacter.mjs`` with the numbers
in ``config/actors/player-slime.actor.json``). Both layers map onto Lyra one-to-one, so this
script rebuilds them as editor assets instead of hand-clicking them:

    IMC_OceanPlayerControl (priority 2, injected by the OceanAdventure GameFeature)
      LeftShift / RightShift -> IA_Player_Sprint   -> InputTag.Player.Sprint  (native slot)
      F                      -> IA_Player_Interact -> InputTag.Ability.Interact
      Q                      -> IA_Player_Drop     -> InputTag.Ability.Quickslot.Drop

    DA_InputConfig_OceanAdventure
      NativeInputActions  += sprint
      AbilityInputActions += interact, drop

    BP_OceanAdventure_Pawn.CharacterMovement
      the SkyLand walk / acceleration / braking / jump / air numbers, converted m -> cm

Deliberately NOT touched, so this script never fights its neighbours:

* WASD and the camera zoom/rotate actions. ``CreateOceanAdventureExperience.py`` already owns
  them in IMC_OceanAdventure_Base and the keys are identical to SkyLand's, so re-mapping them
  here would only create two owners for one binding. This script audits them and reports.
* SpaceBar -> jump. /Game/Input/IMC_Base already maps it with the same key; only the jump
  *numbers* below differ, and those live on the movement component.
* The capsule. SkyLand's player is a 0.84 m slime, the Ocean pawn is a human-scale Lyra
  character -- shrinking the capsule changes the art and collision, not the feel.

Run from the editor, in the Output Log's **Python input mode** (not ``Cmd`` mode -- ``Cmd``
silently discards ``import``; see python-script-governance PY-UE-002):

    import MigrateSkyLandPlayerControl

To run again in the same editor session:

    import importlib, MigrateSkyLandPlayerControl
    importlib.reload(MigrateSkyLandPlayerControl)

Success is the final ``SKYLAND_PLAYER_CONTROL_OK`` line. Without that line the run did not
succeed, even when nothing looks wrong above it.

Requires: InputTag.Player.Sprint registered (Config/DefaultGameplayTags.ini), the
OceanAdventureRuntime module compiled, and CreateOceanAdventureExperience.py already run so
the InputConfig, the pawn blueprint and the GameFeatureData exist.

Idempotent: assets are created when missing; the entries this script owns are rebuilt by
GameplayTag value, never appended to. Running it twice leaves identical arrays.
"""

import unreal


INPUT_ROOT = "/OceanAdventure/Input"
INPUT_CONFIG_PATH = f"{INPUT_ROOT}/DA_InputConfig_OceanAdventure"
INPUT_MAPPING_PATH = f"{INPUT_ROOT}/IMC_OceanPlayerControl"
BASE_INPUT_MAPPING_PATH = f"{INPUT_ROOT}/IMC_OceanAdventure_Base"
PAWN_BLUEPRINT_PATH = "/OceanAdventure/Character/BP_OceanAdventure_Pawn"
GAME_FEATURE_DATA_PATH = "/OceanAdventure/OceanAdventure"

# IMC_Base sits at 0 and IMC_OceanAdventure_Base at 1. At equal priority the older context
# keeps the key, so this one sits above both -- it claims F, Q and Shift, which IMC_Base also
# uses for its own bindings.
INPUT_MAPPING_PRIORITY = 2
INPUT_MAPPING_ACTION_NAME = "SkyLandPlayerControl_AddInputMapping"

SUCCESS_MARKER = "SKYLAND_PLAYER_CONTROL_OK"

# slot: "native"  -> ULyraInputConfig::NativeInputActions, bound by a component via its tag.
#       "ability" -> ULyraInputConfig::AbilityInputActions, routed to the ASC by tag.
INPUT_SPECS = [
    {
        "asset_name": "IA_Player_Sprint",
        "value_type": unreal.InputActionValueType.BOOLEAN,
        "tag": "InputTag.Player.Sprint",
        "slot": "native",
        # SkyLand: Sprint.Keyboard.Primary / Sprint.Keyboard.Alternate.
        "keys": ["LeftShift", "RightShift"],
    },
    {
        "asset_name": "IA_Player_Interact",
        "value_type": unreal.InputActionValueType.BOOLEAN,
        # Lyra's own E -> IA_Interact entry stays in the InputConfig; this adds SkyLand's F
        # as a second key for the same tag rather than taking E away.
        "tag": "InputTag.Ability.Interact",
        "slot": "ability",
        "keys": ["F"],
    },
    {
        "asset_name": "IA_Player_Drop",
        "value_type": unreal.InputActionValueType.BOOLEAN,
        "tag": "InputTag.Ability.Quickslot.Drop",
        "slot": "ability",
        "keys": ["Q"],
    },
]

# SkyLand works in metres and metres/second; Unreal works in centimetres. Every number below
# is the source value x100, except the two that are ratios.
#
#   walkSpeed 3.2 m/s                 -> MaxWalkSpeed 320
#   acceleration 28 m/s^2             -> MaxAcceleration 2800
#   deceleration 24 m/s^2             -> BrakingDecelerationWalking 2400
#   jump impulse 7 m/s                -> JumpZVelocity 700
#   gravity 22 m/s^2                  -> GravityScale 22 / 9.8
#   air accel 8 x airControl 0.85     -> AirControl 680 / 2800
#   AUTOSTEP_MAX_HEIGHT 0.35 m        -> MaxStepHeight 35
#
# The friction pair is the part that is easy to get wrong. ``stepCharacter`` moves the
# velocity vector toward the target at a constant rate -- there is no term proportional to
# speed. Unreal reproduces that only with both frictions at zero: GroundFriction 0 leaves
# turning to MaxAcceleration alone (a full reversal takes 2 x 320 / 2800 = 0.23 s, same as
# SkyLand), and separate braking friction at 0 leaves stopping to
# BrakingDecelerationWalking alone (0.13 s from full speed).
MOVEMENT_FLOAT_SPECS = [
    ("max_walk_speed", 320.0, "playerMovement.walkSpeed 3.2 m/s"),
    ("max_acceleration", 2800.0, "playerMovement.acceleration 28 m/s^2"),
    ("braking_deceleration_walking", 2400.0, "playerMovement.deceleration 24 m/s^2"),
    ("braking_friction", 0.0, "SkyLand brakes at a constant rate, with no speed-proportional term"),
    ("ground_friction", 0.0, "turning comes from MaxAcceleration, matching moveVectorTowards"),
    ("jump_z_velocity", 700.0, "playerJump.impulse 7 m/s"),
    ("gravity_scale", 2.2449, "playerJump.gravity 22 m/s^2 over Unreal's 9.8"),
    ("air_control", 0.2429, "airAcceleration 8 x airControl 0.85 = 6.8 m/s^2 of air accel"),
    ("max_step_height", 35.0, "characterParams.AUTOSTEP_MAX_HEIGHT 0.35 m"),
]

MOVEMENT_BOOL_SPECS = [
    (
        ("use_separate_braking_friction", "b_use_separate_braking_friction"),
        True,
        "braking must not inherit GroundFriction, which this script sets to 0",
    ),
]

# characterParams.MAX_SLOPE_CLIMB_ANGLE = pi / 3.
WALKABLE_FLOOR_ANGLE = 60.0
WALKABLE_FLOOR_Z = 0.5

# The keys IMC_OceanAdventure_Base owns, mirrored from SkyLand's IMC.Gameplay. The audit at
# the end reports on these; it never rewrites them.
AUDITED_BASE_MAPPINGS = [
    ("IA_OceanAdventure_MoveForward", "W"),
    ("IA_OceanAdventure_MoveBackward", "S"),
    ("IA_OceanAdventure_MoveLeft", "A"),
    ("IA_OceanAdventure_MoveRight", "D"),
    ("IA_OceanAdventure_TopDownCameraZoom", "MouseWheelAxis"),
    ("IA_OceanAdventure_TopDownCameraRotateHold", "RightMouseButton"),
    ("IA_OceanAdventure_TopDownCameraRotate", "Mouse2D"),
]


def log(message):
    unreal.log(f"[SkyLandPlayerControl] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def require_type(type_name, source):
    found = getattr(unreal, type_name, None)
    if found is None:
        raise RuntimeError(
            f"unreal.{type_name} is missing. Compile {source} and restart the editor."
        )
    return found


def fail_if_in_play_in_editor():
    """PIE makes EditorAssetLibrary return falsy values that read as 'asset missing'."""
    subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if subsystem is not None and subsystem.is_in_play_in_editor():
        raise RuntimeError(
            "The editor is in Play In Editor. Click Stop and run this script again -- "
            "asset queries return misleading results during PIE."
        )


def load_existing(asset_path_name):
    package_path, _, _ = asset_path_name.rpartition("/")
    if package_path:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        registry.scan_paths_synchronous([package_path], True, True)
    if not unreal.EditorAssetLibrary.does_asset_exist(asset_path_name):
        return None
    return unreal.EditorAssetLibrary.load_asset(asset_path_name)


def split_path(asset_path_name):
    package_path, _, asset_name = asset_path_name.rpartition("/")
    return package_path, asset_name


def save(asset_path_name):
    require(
        unreal.EditorAssetLibrary.save_asset(asset_path_name, only_if_is_dirty=False),
        f"Failed to save: {asset_path_name}",
    )


def get_or_create_data_asset(asset_path_name, asset_class, factory=None):
    existing = load_existing(asset_path_name)
    if existing is not None:
        return existing

    package_path, asset_name = split_path(asset_path_name)
    if factory is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", asset_class)

    asset = require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_path, asset_class, factory
        ),
        f"Failed to create: {asset_path_name}",
    )
    log(f"Created {asset_path_name}")
    return asset


def make_key(key_name):
    key = unreal.Key()
    key.set_editor_property("key_name", unreal.Name(key_name))
    return key


def gameplay_tag(tag_name):
    """Resolve a registered tag across UE 5.7 Python wrapper variants."""
    request_tag = getattr(unreal.GameplayTagLibrary, "request_gameplay_tag", None)
    if request_tag is not None:
        tag = request_tag(unreal.Name(tag_name), False)
    else:
        tag = unreal.GameplayTag()
        tag.import_text(tag_name)

    is_valid = getattr(unreal.GameplayTagLibrary, "is_gameplay_tag_valid", None)
    if tag == unreal.GameplayTag() or (is_valid is not None and not is_valid(tag)):
        raise RuntimeError(
            f"GameplayTag is not registered: {tag_name}. Add it to "
            "Config/DefaultGameplayTags.ini and restart the editor."
        )
    return tag


def asset_path(asset):
    """Stable package path; UObject wrapper equality is unreliable."""
    if asset is None:
        return ""
    get_path_name = getattr(asset, "get_path_name", None)
    if get_path_name is None:
        return str(asset)
    return str(get_path_name()).split(".", 1)[0]


def gameplay_tags_equal(left, right):
    """Compare FGameplayTag values; the generated ``==`` may compare wrapper identity."""
    equal_tag = getattr(unreal.GameplayTagLibrary, "equal_equal_gameplay_tag", None)
    if equal_tag is not None:
        return bool(equal_tag(left, right))

    export_left = getattr(left, "export_text", None)
    export_right = getattr(right, "export_text", None)
    if export_left is not None and export_right is not None:
        return str(export_left()) == str(export_right())
    return left == right


def has_input_entry(entries, action, input_tag):
    expected_action_path = asset_path(action)
    return any(
        asset_path(entry.get_editor_property("input_action")) == expected_action_path
        and gameplay_tags_equal(entry.get_editor_property("input_tag"), input_tag)
        for entry in entries
    )


def rebuild_owned_entries(input_config, property_name, owned):
    """Replace this script's entries in one InputConfig array, keeping every other entry.

    ``in`` / ``not in`` would compare wrapped structs by identity here: the filter would never
    match, the rebuild would degrade into an append, and the array would grow by one copy per
    run (python-script-governance PY-LYRA-004).
    """
    existing = list(input_config.get_editor_property(property_name))
    # Matched on action *and* tag, which is narrower than the base script's tag-only filter.
    # IA_Player_Interact deliberately shares InputTag.Ability.Interact with Lyra's own
    # E -> IA_Interact entry; filtering by tag alone would delete that entry, and E would
    # quietly stop interacting on the next run of this script.
    preserved = [
        entry
        for entry in existing
        if not any(
            gameplay_tags_equal(entry.get_editor_property("input_tag"), input_tag)
            and asset_path(entry.get_editor_property("input_action")) == asset_path(action)
            for action, input_tag in owned
        )
    ]
    appended = [
        unreal.LyraInputAction(input_action=action, input_tag=input_tag)
        for action, input_tag in owned
    ]
    input_config.set_editor_property(property_name, preserved + appended)

    configured = list(input_config.get_editor_property(property_name))
    if len(configured) != len(preserved) + len(appended):
        raise RuntimeError(
            f"{property_name} length is {len(configured)}, expected "
            f"{len(preserved) + len(appended)} -- the rebuild appended instead of replacing."
        )
    for action, input_tag in owned:
        if not has_input_entry(configured, action, input_tag):
            raise RuntimeError(
                f"{property_name} did not retain {action.get_name()} -> {input_tag}"
            )
    return len(preserved), len(appended)


def configure_input_assets():
    input_config = require(
        load_existing(INPUT_CONFIG_PATH),
        f"Missing {INPUT_CONFIG_PATH}. Run CreateOceanAdventureExperience.py first.",
    )

    input_mapping = get_or_create_data_asset(
        INPUT_MAPPING_PATH,
        unreal.InputMappingContext,
        unreal.InputMappingContext_Factory()
        if hasattr(unreal, "InputMappingContext_Factory")
        else None,
    )

    resolved = []
    for spec in INPUT_SPECS:
        action = get_or_create_data_asset(
            f"{INPUT_ROOT}/{spec['asset_name']}",
            unreal.InputAction,
            unreal.InputActionFactory() if hasattr(unreal, "InputActionFactory") else None,
        )
        action.set_editor_property("value_type", spec["value_type"])

        # Unmap first: the mapping list is this script's to own, so a changed key list must
        # not leave the previous key behind.
        input_mapping.unmap_all_keys_from_action(action)
        for key_name in spec["keys"]:
            input_mapping.map_key(action, make_key(key_name))

        resolved.append((spec, action, gameplay_tag(spec["tag"])))

    native_owned = [(action, tag) for spec, action, tag in resolved if spec["slot"] == "native"]
    ability_owned = [(action, tag) for spec, action, tag in resolved if spec["slot"] == "ability"]

    kept, added = rebuild_owned_entries(input_config, "native_input_actions", native_owned)
    log(f"NativeInputActions: kept {kept}, owns {added}")
    kept, added = rebuild_owned_entries(input_config, "ability_input_actions", ability_owned)
    log(f"AbilityInputActions: kept {kept}, owns {added}")

    for spec in INPUT_SPECS:
        save(f"{INPUT_ROOT}/{spec['asset_name']}")
    save(INPUT_MAPPING_PATH)
    save(INPUT_CONFIG_PATH)
    log(
        "Mapped "
        + ", ".join(f"{'/'.join(s['keys'])} -> {s['asset_name']}" for s in INPUT_SPECS)
    )
    return input_mapping


def get_blueprint_class(blueprint, asset_path_name):
    generated = blueprint.generated_class()
    if generated is not None:
        return generated
    return require(
        unreal.EditorAssetLibrary.load_blueprint_class(asset_path_name),
        f"Unable to resolve the generated class of {asset_path_name}",
    )


def resolve_bool_property(movement, candidates):
    """Pick the name this engine build exposes, instead of guessing one of the two."""
    for name in candidates:
        try:
            movement.get_editor_property(name)
        except Exception:
            continue
        return name
    raise RuntimeError(
        "UCharacterMovementComponent exposes none of "
        f"{list(candidates)} to Python. Check the property name in the editor's "
        "Details panel tooltip before editing this script."
    )


def preflight_movement_properties(movement):
    """Read every property first, so a typo fails before anything has been written.

    A half-applied tuning pass is worse than none: the numbers that did land look like a
    deliberate state, and the saved asset no longer matches any single source of truth.
    """
    missing = []
    probed = [name for name, _, _ in MOVEMENT_FLOAT_SPECS]
    probed += ["walkable_floor_angle", "walkable_floor_z"]
    for name in probed:
        try:
            movement.get_editor_property(name)
        except Exception:
            missing.append(name)
    if missing:
        raise RuntimeError(
            "UCharacterMovementComponent does not expose these properties to Python: "
            f"{missing}. Nothing was written."
        )
    return [resolve_bool_property(movement, names) for names, _, _ in MOVEMENT_BOOL_SPECS]


def configure_movement_tuning(pawn_blueprint_class):
    pawn_defaults = unreal.get_default_object(pawn_blueprint_class)
    movement = require(
        pawn_defaults.get_component_by_class(
            require_type("CharacterMovementComponent", "the Engine module")
        ),
        f"{PAWN_BLUEPRINT_PATH} has no CharacterMovement component",
    )

    bool_names = preflight_movement_properties(movement)

    for name, value, reason in MOVEMENT_FLOAT_SPECS:
        movement.set_editor_property(name, value)
        configured = float(movement.get_editor_property(name))
        if abs(configured - value) > 0.001:
            raise RuntimeError(f"{name} did not persist: {configured} != {value} ({reason})")

    for name, (_, value, reason) in zip(bool_names, MOVEMENT_BOOL_SPECS):
        movement.set_editor_property(name, value)
        if bool(movement.get_editor_property(name)) != value:
            raise RuntimeError(f"{name} did not persist ({reason})")

    # WalkableFloorZ is derived from the angle. If the derived value does not follow, the
    # asset is internally inconsistent and the slope limit silently stays at the old value.
    movement.set_editor_property("walkable_floor_angle", WALKABLE_FLOOR_ANGLE)
    configured_z = float(movement.get_editor_property("walkable_floor_z"))
    if abs(configured_z - WALKABLE_FLOOR_Z) > 0.01:
        raise RuntimeError(
            f"WalkableFloorAngle was set to {WALKABLE_FLOOR_ANGLE} but WalkableFloorZ is "
            f"{configured_z}, expected {WALKABLE_FLOOR_Z}. The derived value did not follow; "
            "set the angle in the blueprint's Details panel instead."
        )

    log(
        "Movement: MaxWalkSpeed {0}, MaxAcceleration {1}, BrakingDecelerationWalking {2}, "
        "JumpZVelocity {3}, GravityScale {4}, AirControl {5}".format(
            movement.get_editor_property("max_walk_speed"),
            movement.get_editor_property("max_acceleration"),
            movement.get_editor_property("braking_deceleration_walking"),
            movement.get_editor_property("jump_z_velocity"),
            movement.get_editor_property("gravity_scale"),
            movement.get_editor_property("air_control"),
        )
    )


def configure_game_feature_data(input_mapping):
    game_feature_data = require(
        load_existing(GAME_FEATURE_DATA_PATH),
        f"Missing GameFeatureData: {GAME_FEATURE_DATA_PATH}. Run CreateGameFeatureData first.",
    )

    # Stable name filtering is a plain string comparison, so it is not affected by the
    # wrapper-identity trap that the InputConfig arrays have. Every other action -- including
    # the base script's -- is preserved.
    actions = [
        action
        for action in game_feature_data.get_editor_property("actions")
        if action is not None and str(action.get_name()) != INPUT_MAPPING_ACTION_NAME
    ]
    asset_library = require_type(
        "OceanAdventureAssetLibrary", "the OceanAdventureRuntime module"
    )
    actions.append(
        require(
            asset_library.create_add_input_context_mapping_action(
                game_feature_data,
                input_mapping,
                INPUT_MAPPING_PRIORITY,
                unreal.Name(INPUT_MAPPING_ACTION_NAME),
            ),
            "Failed to create the SkyLand player-control input mapping action",
        )
    )
    game_feature_data.set_editor_property("actions", actions)
    save(GAME_FEATURE_DATA_PATH)

    named = [
        action
        for action in game_feature_data.get_editor_property("actions")
        if action is not None and str(action.get_name()) == INPUT_MAPPING_ACTION_NAME
    ]
    if len(named) != 1:
        raise RuntimeError(
            f"Expected exactly one {INPUT_MAPPING_ACTION_NAME} action, found {len(named)}"
        )
    log(
        f"GameFeatureData adds {INPUT_MAPPING_PATH} at priority {INPUT_MAPPING_PRIORITY} "
        f"as {INPUT_MAPPING_ACTION_NAME}"
    )


def audit_base_layout():
    """Report whether the WASD/camera keys still match SkyLand's. Never rewrites them.

    Reading FEnhancedActionKeyMapping through Python is not something this repository has
    exercised, so a missing property degrades to a skipped audit -- it must never turn a
    successful migration into a failure, and it must never be mistaken for a pass either.
    """
    base_mapping = load_existing(BASE_INPUT_MAPPING_PATH)
    if base_mapping is None:
        log(f"AUDIT SKIPPED: {BASE_INPUT_MAPPING_PATH} not found")
        return

    try:
        mappings = list(base_mapping.get_editor_property("mappings"))
        actual = set()
        for mapping in mappings:
            action = mapping.get_editor_property("action")
            key = mapping.get_editor_property("key")
            actual.add(
                (
                    str(asset_path(action).rpartition("/")[2]),
                    str(key.get_editor_property("key_name")),
                )
            )
    except Exception as error:
        log(f"AUDIT SKIPPED: cannot read IMC mappings from Python ({error})")
        return

    missing = [pair for pair in AUDITED_BASE_MAPPINGS if pair not in actual]
    if missing:
        log(
            "AUDIT: these SkyLand movement/camera keys are not in "
            f"{BASE_INPUT_MAPPING_PATH}: {missing}. They belong to "
            "CreateOceanAdventureExperience.py -- fix them there, not here."
        )
    else:
        log("AUDIT: WASD, wheel zoom, right-drag rotate all match the SkyLand layout")


def main():
    fail_if_in_play_in_editor()
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        ["/OceanAdventure"], True, True
    )

    input_mapping = configure_input_assets()

    blueprint = require(
        load_existing(PAWN_BLUEPRINT_PATH),
        f"Missing {PAWN_BLUEPRINT_PATH}. Run CreateOceanAdventureExperience.py first.",
    )
    configure_movement_tuning(get_blueprint_class(blueprint, PAWN_BLUEPRINT_PATH))
    save(PAWN_BLUEPRINT_PATH)

    configure_game_feature_data(input_mapping)
    audit_base_layout()

    log("Restart the editor so the game feature re-registers the new input mapping action.")
    unreal.log(SUCCESS_MARKER)


main()
