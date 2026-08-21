"""Repair OceanAdventure build InputActions so one physical press fires once.

Lyra binds ability actions to ETriggerEvent::Triggered. An InputAction without an
edge trigger therefore calls the ability input path every frame while its key is
held. This script keeps exactly one InputTriggerPressed on each build action and
is safe to run repeatedly.
"""

import unreal


ACTION_PATHS = (
    "/OceanAdventure/Input/IA_Build_Mode",
    "/OceanAdventure/Input/IA_Build_Confirm",
    "/OceanAdventure/Input/IA_Build_Remove",
)
TRIGGER_OBJECT_NAME = unreal.Name("OceanBuild_Pressed")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def configure_pressed_trigger(action, pressed_class):
    triggers = [
        trigger
        for trigger in action.get_editor_property("triggers")
        if trigger is not None
    ]
    pressed_trigger = next(
        (trigger for trigger in triggers if isinstance(trigger, pressed_class)),
        None,
    )
    if pressed_trigger is None:
        pressed_trigger = require(
            unreal.new_object(
                pressed_class,
                outer=action,
                name=TRIGGER_OBJECT_NAME,
            ),
            f"Failed to create InputTriggerPressed for {action.get_path_name()}",
        )

    # These actions are owned by OceanAdventure. Removing legacy triggers makes the
    # one-press contract explicit and prevents duplicate subobjects on repeated runs.
    action.set_editor_property("triggers", [pressed_trigger])
    configured = list(action.get_editor_property("triggers"))
    require(
        len(configured) == 1 and isinstance(configured[0], pressed_class),
        f"{action.get_path_name()} did not retain exactly one InputTriggerPressed",
    )


def main():
    pressed_class = require(
        getattr(unreal, "InputTriggerPressed", None),
        "InputTriggerPressed is unavailable; verify Enhanced Input is enabled",
    )

    repaired = []
    for action_path in ACTION_PATHS:
        action = require(
            unreal.EditorAssetLibrary.load_asset(action_path),
            f"Missing build InputAction: {action_path}",
        )
        configure_pressed_trigger(action, pressed_class)
        require(
            unreal.EditorAssetLibrary.save_loaded_asset(
                action,
                only_if_is_dirty=False,
            ),
            f"Unable to save {action.get_path_name()}",
        )
        repaired.append(action.get_path_name())

    unreal.log(
        "[OceanAdventureBuildInputRepair] Validated one InputTriggerPressed on: "
        + ", ".join(repaired)
    )


if __name__ == "__main__":
    main()
