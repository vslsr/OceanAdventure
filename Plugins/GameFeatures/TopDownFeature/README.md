# Top Down Feature

This GameFeature provides three reusable runtime classes:

- `ULyraCameraMode_TopDownFollow`: smooth zoomable/orbiting camera centered on the current Lyra camera target.
- `UTopDownGameplayAbility_Move`: player-ASC movement intent, granted once per directional InputTag.
- `UTopDownPawnComponent`: movement executor, mouse-facing presentation and local camera input.

## Generated assets

The following assets are included in the plugin:

1. `/TopDownFeature/TopDownFeature` (`UGameFeatureData`).
2. `/TopDownFeature/Input/IA_TopDownMove*` plus zoom, rotate-hold and pointer-delta `UInputAction` assets.
3. `/TopDownFeature/Input/IMC_TopDown`, mapping W/A/S/D, mouse wheel, right click and mouse delta.
4. `/TopDownFeature/Input/DA_TopDown_InputConfig`: movement in `AbilityInputActions`, camera in `NativeInputActions`.
5. `/TopDownFeature/Abilities/DA_AbilitySet_TopDownMovement`, granting the same movement Ability for the four direction tags.
6. `/TopDownFeature/Pawn/DA_TopDown_PawnData`, using the project-owned default hero pawn and `ULyraCameraMode_TopDownFollow` as `DefaultCameraMode`.

The generated PawnData intentionally has no feature AbilitySet. `TopDownFeature` grants it to
`ALyraPlayerState` through `GameFeatureAction_AddAbilities`, so consuming PawnData assets do not
need sibling-GameFeature asset references and cannot accidentally grant duplicate specs.

`Content/Python/create_top_down_assets.py` recreates or updates these assets and is safe to run repeatedly after compiling the runtime module.

The script idempotently configures `/TopDownFeature/TopDownFeature` as follows:

```text
Add Components
  ActorClass     = ALyraCharacter
  ComponentClass = UTopDownPawnComponent

Add Input Mapping
  InputMapping = IMC_TopDown
  Priority     = 1

Add Input Binding
  InputConfig = DA_TopDown_InputConfig

Add Abilities
  ActorClass        = ALyraPlayerState
  GrantedAbilitySet = DA_AbilitySet_TopDownMovement
```

Duplicate the Experience currently used by the target game mode, set its `DefaultPawnData` to `DA_TopDown_PawnData`, and add `TopDownFeature` to `GameFeaturesToEnable`. Preserve the source Experience's existing Actions and ActionSets. If the target uses a pawn other than the SimpleExperience pawn, also update `DA_TopDown_PawnData.PawnClass` before assigning the Add Components action.

The target pawn must be a ModularGameplay component receiver and own a `ULyraCameraComponent`, `ULyraHeroComponent`, `ULyraPawnExtensionComponent`, `ULyraInputComponent`, and a movement component that consumes `AddMovementInput`.

## Runtime behavior

The component waits for Lyra's `BindInputsNow` extension event before binding only the camera-native actions. If injected after that event, it uses `ULyraHeroComponent::IsReadyToBindInputs()` immediately. Mouse-wheel zoom is clamped and smoothed by the camera mode. Holding right mouse pushes a CommonUI game-capture policy; horizontal pointer delta rotates the camera until release, when the prior CommonUI policy is restored.

WASD is camera-relative, but the component no longer owns those bindings. Each held direction activates a predicted movement Ability that sets one executor intent and ends on release. `Gameplay.MovementStopped` both blocks new activation and cancels active directions, which lets naval/building station abilities take movement ownership without hard-coded input switching. The old target-movement Blueprint functions remain compatibility APIs; no click action is mapped or bound.
