# Direct-Map Experience Debugging

Use this reference when a GameFeature-owned map should start its own Lyra
Experience directly, or when `Default Gameplay Experience` is configured but
PIE still enters the frontend Menu.

## Ownership And Bootstrap

Keep the responsibilities separate:

| Owner | Responsibility |
| --- | --- |
| Project AssetManager settings | Declare global Primary Asset types and their shared cook rules |
| GameFeatureData | Add scan paths for assets owned by that plugin |
| Map `ALyraWorldSettings` | Select the default Experience for direct map startup |
| `ULyraExperienceDefinition` Blueprint | Compose PawnData, ActionSets, Actions, and GameFeatures to activate |
| `ULyraUserFacingExperienceDefinition` DataAsset | Advertise a map/Experience pair to the frontend |

Do not add plugin-specific directories to `DefaultGame.ini` when the plugin's
GameFeatureData can own them. In this project, the global `Map`,
`LyraExperienceDefinition`, and `LyraUserFacingExperienceDefinition` types
already exist in AssetManager settings with shared cook rules. GameFeatureData
only extends their scan paths. An `Unknown` cook rule displayed on the plugin's
path entry is not by itself evidence that these assets will not cook; UE reuses
the existing type rule when that Primary Asset type was already declared.

For an OceanAdventure-style plugin, the GameFeatureData scan entries are:

| Primary Asset type | Base class | Blueprint classes | Directory |
| --- | --- | --- | --- |
| `Map` | `World` | false | `/OceanAdventure/Maps` |
| `LyraExperienceDefinition` | `LyraExperienceDefinition` | true | `/OceanAdventure/Experience` |
| `LyraUserFacingExperienceDefinition` | `LyraUserFacingExperienceDefinition` | false | `/OceanAdventure/Experience` |

Add `LyraExperienceActionSet` only when the plugin owns ActionSet assets in a
plugin directory. Project-owned ActionSets already covered by the project scan
do not require another plugin rule.

The plugin must reach `Registered` before Lyra selects the Experience:

```text
Installed
  -> Registering
     -> mount plugin content
     -> add GameFeatureData scan paths to AssetManager
     -> ScanPathsForPrimaryAssets
  -> Registered
  -> Loading -> Loaded -> Activating -> Active
```

This breaks the bootstrap cycle. AssetManager must recognize the Experience
before the map can select it, but the Experience cannot activate its listed
GameFeatures until after it has been selected. Therefore
`BuiltInInitialFeatureState=Registered` discovers assets without applying
gameplay Actions. Experience loading later moves the required plugin to
`Active`.

## Required Asset Shapes

`LyraExperienceDefinition` is scanned with Blueprint classes enabled. The
gameplay Experience must therefore be a Blueprint whose parent is
`ULyraExperienceDefinition`; its class defaults hold `DefaultPawnData`,
`ActionSets`, `Actions`, and `GameFeaturesToEnable`.

Do not create an object instance DataAsset and merely give it a `BP_` prefix.
It cannot be selected as the map's Experience class. If a creation tool reports
that an existing asset is `LyraExperienceDefinition` but expected `Blueprint`,
delete the wrong-typed asset in the editor and recreate it as a Blueprint.
Avoid editing or deleting `.uasset` binaries directly.

The frontend entry is different: it is a
`ULyraUserFacingExperienceDefinition` DataAsset. Its `MapID` and `ExperienceID`
are Primary Asset IDs, not substitutes for the map's World Settings value.

For direct startup, save this on the intended map:

```text
World Settings
  Game Mode Override = None
  Default Gameplay Experience = <Experience Blueprint class>
```

`Game Mode Override=None` is normal when the project-wide GameMode is Lyra's
GameMode. The critical setting is the generated Experience class.

## Refresh After Creating Assets

GameFeatureData scan paths are applied when the plugin transitions through
`Registering`. Creating or replacing an Experience after the plugin is already
`Registered` does not replay that transition. Moving straight to `Active` also
does not rescan; activation only applies GameFeature Actions.

Refresh through GameFeature state control without restarting:

1. Stop PIE and save the GameFeatureData, Experience, and map.
2. Switch to a map outside the GameFeature so its content can unmount cleanly.
3. Open the plugin's GameFeatureData state control.
4. Change `Registered -> Installed` and wait for completion.
5. Change `Installed -> Registered` and wait for completion.
6. Reopen the plugin map and start PIE.

The transition removes and re-adds the plugin scan paths, then scans assets
that now exist. Restarting the editor is only a fallback that causes the same
registration work during startup; it is not a runtime architecture requirement.

## PIE Versus Game Startup

PIE from the current level duplicates that editor world, so its World Settings
can select the Experience. Standalone launch, Launch, or a packaged executable
first loads `GameDefaultMap`; a World Settings value on another map cannot
affect that selection. To make direct game startup open the gameplay map,
change `GameDefaultMap` or launch with an explicit map URL. Do not change the
frontend default when the requirement is only faster editor iteration.

Also verify the full package path when two plugins contain maps with the same
asset name. A Primary Asset ID is `(type, name)` and does not include the path;
scanning two same-named maps creates an ambiguous ID. In this project,
`/OceanAdventure/Maps/L_OceanChunkTest` is the gameplay-owned map; do not
accidentally debug or scan the same-named OceanCore map as another `Map`
Primary Asset.

## Binary Log Judgement

Start with `LogLyraExperience`, the actual loaded map, and the Experience source.

Failure before Experience loading:

```text
DefaultGameplayExperience is ..._C but that failed to resolve into an asset ID
Identified experience LyraExperienceDefinition:BP_Experience_Default (Source: Default)
```

This means the map reference exists, but AssetManager does not know that class
as a Primary Asset. Check the Blueprint shape and scan rule, then re-register
the GameFeature. Do not debug `W_MainMenu`; the Menu is only the downstream
fallback Experience behaving correctly.

Successful direct-map selection:

```text
Identified experience LyraExperienceDefinition:<name> (Source: WorldSettings)
EXPERIENCE: OnExperienceLoadComplete(CurrentExperience = LyraExperienceDefinition:<name>, ...)
```

If the source is `OptionsString`, `DeveloperSettings`, or `CommandLine`, that
higher-precedence override is intentionally winning over World Settings. If the
source is `WorldSettings` but gameplay is incomplete, inspect the selected
Experience's PawnData, ActionSets, Actions, and activated GameFeatures rather
than its Primary Asset registration.

## Stable Source Anchors

- `Source/LyraGame/GameModes/LyraGameMode.cpp`:
  `HandleMatchAssignmentIfNotExpectingOne` defines selection precedence and the
  default fallback.
- `Source/LyraGame/GameModes/LyraWorldSettings.cpp`:
  `GetDefaultGameplayExperience` converts the soft class path to a Primary Asset ID.
- UE GameFeatures `GameFeaturesSubsystem.cpp`:
  `AddGameFeatureToAssetManager` scans GameFeatureData paths during registration;
  `RemoveGameFeatureFromAssetManager` removes them during unregistration.
- UE GameFeatures `GameFeaturePluginStateMachine.cpp`:
  the Registering state calls `AddGameFeatureToAssetManager` before becoming
  Registered.
