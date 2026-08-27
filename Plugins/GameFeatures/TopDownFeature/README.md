# Top Down Feature

This GameFeature provides two reusable runtime classes:

- `ULyraCameraMode_TopDownFollow`: smooth zoomable/orbiting camera centered on the current Lyra camera target.
- `UTopDownPawnComponent`: mouse-facing WASD movement plus local camera input using the pawn's existing Lyra input component.

## Generated assets

The following assets are included in the plugin:

1. `/TopDownFeature/TopDownFeature` (`UGameFeatureData`).
2. `/TopDownFeature/Pawn/DA_TopDown_PawnData`, using the project-owned default hero pawn,
   the project base InputConfig, and `ULyraCameraMode_TopDownFollow` as `DefaultCameraMode`.

The generated PawnData intentionally has no AbilitySets. This keeps the reusable
GameFeature free of illegal references to a sibling feature such as `SimpleExperience`.
A gameplay-specific consumer should own a derived PawnData when it needs that feature's
pawn class, AbilitySets, or TopDown input bindings.

`Content/Python/create_top_down_assets.py` recreates or updates these assets and is safe to
run repeatedly after compiling the runtime module. It also migrates away the obsolete
`IMC_TopDown` and `DA_TopDown_InputConfig`: references are cleared and saved before those
two assets are deleted.

## Editor configuration

The GameFeature actions remain project-specific because `ActorClass` must match the pawn used by the selected Experience. Configure `/TopDownFeature/TopDownFeature` as follows:

Configure the GameFeatureData action as follows:

```text
Add Components
  ActorClass     = actual player pawn class (ALyraCharacter or the concrete vehicle/boat pawn)
  ComponentClass = UTopDownPawnComponent
```

The GameFeature that owns the player Pawn must also own the relevant `UInputAction`,
`UInputMappingContext`, and `ULyraInputConfig` assets. It injects that mapping and registers
the TopDown tags as `NativeInputActions`; `TopDownFeature` must not inject a second mapping.
In OceanAdventure this is handled by `IMC_OceanAdventure_Base` and
`DA_InputConfig_OceanAdventure`.

Duplicate the Experience currently used by the target game mode, use the consuming
gameplay feature's PawnData, and add `TopDownFeature` to `GameFeaturesToEnable`. Preserve
the source Experience's existing Actions and ActionSets.

The target pawn must be a ModularGameplay component receiver and own a `ULyraCameraComponent`, `ULyraHeroComponent`, `ULyraPawnExtensionComponent`, `ULyraInputComponent`, and a movement component that consumes `AddMovementInput`.

## Runtime behavior

The component waits for Lyra's `BindInputsNow` extension event before binding its native actions. If the component is injected after that event, it uses `ULyraHeroComponent::IsReadyToBindInputs()` to bind immediately. Mouse-wheel zoom is clamped and smoothed by the camera mode. Holding right mouse pushes a CommonUI game-capture policy; horizontal pointer delta rotates the camera until release, when the prior CommonUI policy is restored. Input handles and UI state are also restored when the component or GameFeature is removed.

WASD movement is camera-relative and is bound as four 1D native actions supplied by the
consuming gameplay feature. The local pawn's actor yaw follows the deprojected mouse ray
intersected with the pawn's XY plane; holding right mouse for camera rotation temporarily
pauses facing updates. The old target-movement Blueprint functions remain as compatibility
APIs, but no click action is mapped or bound by this plugin.
