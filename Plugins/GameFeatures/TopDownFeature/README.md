# Top Down Feature

This GameFeature provides two reusable runtime classes:

- `ULyraCameraMode_TopDownFollow`: smooth zoomable/orbiting camera centered on the current Lyra camera target.
- `UTopDownPawnComponent`: optional direct click-to-move plus local camera input using the pawn's existing Lyra input component.

## Generated assets

The following assets are included in the plugin:

1. `/TopDownFeature/TopDownFeature` (`UGameFeatureData`).
2. `/TopDownFeature/Input/IA_TopDownClick` plus zoom, rotate-hold and pointer-delta `UInputAction` assets.
3. `/TopDownFeature/Input/IMC_TopDown`, mapping left click, mouse wheel, right click and mouse delta.
4. `/TopDownFeature/Input/DA_TopDown_InputConfig`, registering all four actions by native `InputTag`.
5. `/TopDownFeature/Pawn/DA_TopDown_PawnData`, using the project-owned default hero pawn and `ULyraCameraMode_TopDownFollow` as `DefaultCameraMode`.

The generated PawnData intentionally has no AbilitySets. This keeps the reusable
GameFeature free of illegal references to a sibling feature such as `SimpleExperience`.
A gameplay-specific consumer should own a derived PawnData when it needs that feature's
pawn class or AbilitySets.

`Content/Python/create_top_down_assets.py` recreates or updates these assets and is safe to run repeatedly after compiling the runtime module.

## Editor configuration

The GameFeature actions remain project-specific because `ActorClass` must match the pawn used by the selected Experience. Configure `/TopDownFeature/TopDownFeature` as follows:

Configure the GameFeatureData actions as follows:

```text
Add Components
  ActorClass     = actual player pawn class (ALyraCharacter or the concrete vehicle/boat pawn)
  ComponentClass = UTopDownPawnComponent

Add Input Mapping
  InputMapping = IMC_TopDown
  Priority     = 0
```

Duplicate the Experience currently used by the target game mode, set its `DefaultPawnData` to `DA_TopDown_PawnData`, and add `TopDownFeature` to `GameFeaturesToEnable`. Preserve the source Experience's existing Actions and ActionSets. If the target uses a pawn other than the SimpleExperience pawn, also update `DA_TopDown_PawnData.PawnClass` before assigning the Add Components action.

The target pawn must be a ModularGameplay component receiver and own a `ULyraCameraComponent`, `ULyraHeroComponent`, `ULyraPawnExtensionComponent`, `ULyraInputComponent`, and a movement component that consumes `AddMovementInput`.

## Runtime behavior

The component waits for Lyra's `BindInputsNow` extension event before binding its native actions. If the component is injected after that event, it uses `ULyraHeroComponent::IsReadyToBindInputs()` to bind immediately. Mouse-wheel zoom is clamped and smoothed by the camera mode. Holding right mouse pushes a CommonUI game-capture policy; horizontal pointer delta rotates the camera until release, when the prior CommonUI policy is restored. Input handles and UI state are also restored when the component or GameFeature is removed.

Click movement is intentionally direct movement, not pathfinding. It runs only for the locally controlled pawn and relies on the existing movement component's prediction and replication. Add NavMesh path following and server-side target validation as a separate multiplayer feature.
