# Ocean Adventure

The game feature that defines the Ocean Adventure mode: its player pawn, and the wiring
that gives that pawn its capabilities.

## Design

`AOceanAdventurePawn` is deliberately thin. It provides a stable native target for
experience-specific component injection and runtime diagnostics, but owns no gameplay
capability or tuning values. Capabilities arrive as components injected when the experience
activates the features; pawn tuning remains on `BP_OceanAdventure_Pawn`:

```
BP_Experience_Ocean  (LyraExperienceDefinition)
│
├─ DefaultPawnData ──────► DA_OceanAdventure_PawnData
│                            PawnClass         ──► BP_OceanAdventure_Pawn (AOceanAdventurePawn)
│                            AbilitySets       ──► empty until the mode has real Ocean-owned abilities
│                            DefaultCameraMode ──► ULyraCameraMode_TopDownFollow
│                            InputConfig       ──► /Game/Input/DA_InputConfig_Base
│
├─ ActionSets ───────────► LSA_Standard_Components / LSA_Shared_Input / LAS_Standard_HUD
│
└─ GameFeaturesToEnable
   ├─ "OceanAdventure"  ──► AddComponents: LyraGameState       ← UOceanWorldManagerComponent
   │                        AddComponents: AOceanAdventurePawn ← UOceanChunkInvokerComponent
   └─ "TopDownFeature"  ──► AddComponents: LyraCharacter       ← UTopDownPawnComponent
                            AddInputMapping: IMC_TopDown
```

Adding or dropping a capability is an edit to the experience, not to the pawn class.

### Why the experience enables its own plugin

`GameFeaturesToEnable` lists `OceanAdventure` itself. A game feature's actions only run
once the feature is *activated*; being merely registered makes its content discoverable
but does not run anything. Lyra's own ShooterCore experiences do the same.

### Why the chunk components come from a plain plugin

`UOceanChunkInvokerComponent` and `UOceanWorldManagerComponent` live in `OceanCore`, a
plain plugin, so the ocean algorithms stay reusable and free of Lyra coupling.
`GameFeatureAction_AddComponents` accepts any `TSubclassOf<UActorComponent>`, so OceanCore
does not need to be a game feature to be injected -- this plugin activates and its action
does the injecting. The dependency direction stays one-way: game feature → plain plugin.

### Why this mode needs its own PawnData

The top down camera is not a component. Lyra resolves it from
`PawnData.DefaultCameraMode`, so a mode that wants the top down view must say so in its
own PawnData. `/TopDownFeature/Pawn/DA_TopDown_PawnData` cannot serve here: it borrows
SimpleExperience's pawn, and TopDownFeature's own README asks consumers to own a derived
PawnData instead.

`OceanAdventureRuntime` still has no build dependency on `TopDownFeatureRuntime` or
`OceanCoreRuntime` -- the references are data-level, set on the assets by the script below.

## Setup

1. Compile. `OceanAdventureRuntime` is new, so regenerate project files first.
2. Run `CreateGameFeatureData` from the editor Python console if
   `/OceanAdventure/OceanAdventure` does not exist yet.
3. Run the wiring script:

   ```python
   import CreateOceanAdventureExperience
   ```

   It creates `BP_OceanAdventure_Pawn` and `DA_OceanAdventure_PawnData`, puts the
   AddComponents action on the GameFeatureData, sets the pawn Blueprint's
   `MaxSwimSpeed` to `600`, and fills in the experience. PawnData `AbilitySets` remains
   intentionally empty until the mode has real Ocean-owned abilities. It is safe to re-run;
   it rewrites the GameFeatureData's `Actions`, PawnData `AbilitySets`, and the experience's
   `GameFeaturesToEnable` / `ActionSets` wholesale.
4. Restart the editor so the features re-register with the new actions.
5. Open the mode's map and set World Settings → `Default Gameplay Experience` to
   `BP_Experience_Ocean`.

Then give `BP_OceanAdventure_Pawn` a mesh, an animation blueprint and a capsule size, and
tune its remaining CharacterMovement settings for the water.

## Verifying the injection

```
log LogOceanAdventure Verbose
```

On spawn the pawn logs every component attached to it. `UOceanChunkInvokerComponent` and
`UTopDownPawnComponent` in that list means both features activated and their actions ran.
