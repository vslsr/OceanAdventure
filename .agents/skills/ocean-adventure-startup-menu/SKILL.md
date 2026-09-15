---
name: ocean-adventure-startup-menu
description: Explain, trace, modify, or troubleshoot OceanAdventure's Lyra/CommonUI startup and direct-map Experience flow, including the default frontend map, GameFeature-owned Experience discovery, fallback-to-menu behavior, press-start and main screens, UI policy/layers, session travel, and the separate in-game Escape menu. Use when asking where a startup or Escape menu is configured, how to bypass StartMenu for level debugging, or why a map with Default Gameplay Experience still opens Menu. Use general Lyra or UE skills for unrelated gameplay and GameFeature work.
---

# OceanAdventure Startup Menu

Use the project files as the source of truth. This project keeps its frontend assets in the main `/Game` content tree rather than a Lyra `Frontend` GameFeature plugin.

## Current Configuration

### Project startup

`Config/DefaultEngine.ini`, section `[/Script/EngineSettings.GameMapsSettings]`:

- `GlobalDefaultGameMode=/Game/Default/BP_Default_GameMode.BP_Default_GameMode_C`
- `EditorStartupMap=/Game/Map/L_Map_Default.L_Map_Default`
- `GameDefaultMap=/Game/Map/L_Map_Default.L_Map_Default`
- `GameInstanceClass=/Game/Default/BP_Default_GameInstance.BP_Default_GameInstance_C`

`Config/DefaultGame.ini`, section `[/Script/LyraGame.LyraFrameworkSettings]`:

- `DefaultExperienceId=LyraExperienceDefinition:BP_Experience_Default`

`Config/DefaultGame.ini`, section `[/Script/CommonGame.CommonGameSettings]`:

- `DefaultUIPolicyClass=/Game/UI/BP_UIPolicy.BP_UIPolicy_C`

### Frontend assets

| Role | Asset | Current setting |
| --- | --- | --- |
| Frontend map | `/Game/Map/L_Map_Default` | Project editor and game default map |
| Default Experience | `/Game/Experience/BP_Experience_Default` | Adds `/Game/UI/BP_Frontend_State_Component` to `LyraGameState` through `GameFeatureAction_AddComponents` |
| Frontend state | `/Game/UI/BP_Frontend_State_Component` | Blueprint subclass of `ULyraFrontendStateComponent` |
| Press-start screen | `/Game/UI/Splash/W_Startup_Screen` | `PressStartScreenClass` |
| Main screen | `/Game/UI/W_MainMenu` | `MainScreenClass` |
| UI policy | `/Game/UI/BP_UIPolicy` | `LayoutClass=/Game/UI/W_UILayout_Overall` |
| Root layout | `/Game/UI/W_UILayout_Overall` | Registers `UI.Layer.Game`, `UI.Layer.GameMenu`, `UI.Layer.Menu`, `UI.Layer.Modal`, and `UI.Layer.Toast` |
| User-facing frontend definition | `/Game/Experience/DA_Facing_Experience_Default` | Associates `L_Map_Default`, `BP_Experience_Default`, and `/Game/UI/Splash/W_LoadingScreen` for travel/session presentation |

The native default of `bForceShowPressStartScreen` is false. Verify the Blueprint Class Defaults before relying on it because `BP_Frontend_State_Component` can override the native value. The platform/CommonUser policy can also require the press-start screen.

## Runtime Flow

```text
GameDefaultMap: L_Map_Default
  -> BP_Default_GameMode
  -> ALyraGameMode selects BP_Experience_Default
  -> Experience action adds BP_Frontend_State_Component to LyraGameState
  -> component waits for Experience loaded
  -> wait/reset CommonUser and session state
  -> optionally show W_Startup_Screen
  -> optionally join a requested session
  -> push W_MainMenu to UI.Layer.Menu
  -> frontend loading-screen request is released after the widget push
```

The C++ control flow is implemented in:

- `Source/LyraGame/UI/Frontend/LyraFrontendStateComponent.cpp`
  - `BeginPlay`
  - `OnExperienceLoaded`
  - `FlowStep_TryShowPressStartScreen`
  - `FlowStep_TryJoinRequestedSession`
  - `FlowStep_TryShowMainScreen`
- `Source/LyraGame/UI/Frontend/LyraFrontendStateComponent.h`
  - `bForceShowPressStartScreen`
  - `PressStartScreenClass`
  - `MainScreenClass`

`FlowStep_TryShowMainScreen` uses `UPrimaryGameLayout::PushWidgetToLayerStackAsync` and pushes `MainScreenClass` to `UI.Layer.Menu`. The menu is not created by the level Blueprint.

## Experience Selection Precedence

Do not assume `DefaultExperienceId` always wins. `ALyraGameMode::HandleMatchAssignmentIfNotExpectingOne` checks, in order:

1. Matchmaking assignment.
2. URL option `?Experience=...`.
3. PIE `ULyraDeveloperSettings::ExperienceOverride`.
4. Command line `Experience=...`.
5. `ALyraWorldSettings::DefaultGameplayExperience`.
6. Dedicated-server selection.
7. `ULyraFrameworkSettings::DefaultExperienceId` fallback.

When the wrong screen opens, establish which Experience actually loaded before editing menu assets.

## Direct-Map Experience Debugging

For a gameplay map and Experience owned by a GameFeature plugin, read
[references/direct-map-experience.md](references/direct-map-experience.md). It covers:

- why plugin content discovery happens at `Registered` rather than `Active`;
- which scan paths belong in GameFeatureData and which type/cook rules remain project-level;
- the required Blueprint versus DataAsset shapes;
- how to refresh a plugin registration without restarting the editor;
- the log signatures that distinguish a valid World Settings Experience from fallback to the frontend Menu.

Do not solve a Primary Asset discovery failure by changing menu widgets. If
`DefaultGameplayExperience` cannot resolve to an asset ID, Lyra has already
failed before the frontend component or CommonUI layer flow becomes relevant.

## Where To Make Changes

| Goal | Change here |
| --- | --- |
| Change main-menu visuals or button behavior | `/Game/UI/W_MainMenu` |
| Replace the startup main-screen class | `MainScreenClass` in `/Game/UI/BP_Frontend_State_Component` Class Defaults |
| Replace or force the press-start screen | `PressStartScreenClass` or `bForceShowPressStartScreen` in the same component |
| Enable, disable, or replace the complete frontend flow | Actions in `/Game/Experience/BP_Experience_Default`; inspect the Add Components entry for `LyraGameState` |
| Change the frontend map | Maps & Modes or `Config/DefaultEngine.ini` |
| Change the fallback Experience | Lyra Framework Settings or `Config/DefaultGame.ini` |
| Change root CommonUI layer stacks | `/Game/UI/BP_UIPolicy` and `/Game/UI/W_UILayout_Overall` |
| Change session-selection behavior after clicking the main menu | `/Game/UI/W_MainMenu` and `/Game/UI/Session/W_Session_Screen` |

Do not add a second `Create Widget -> Add to Viewport` path in the level Blueprint. It bypasses the CommonUI layer lifecycle, async loading, input suspension, focus handling, and deactivation behavior already provided by the frontend flow.

## Escape Menu Is Separate

The in-game Escape menu is not the startup menu:

```text
/Game/ActionSet/LAS_Standard_HUD
  -> adds /Game/UI/HUD/W_HUDLayout to UI.Layer.Game
  -> ULyraHUDLayout registers UI.Action.Escape
  -> HandleEscapeAction pushes EscapeMenuClass to UI.Layer.Menu
  -> current EscapeMenuClass is /Game/UI/W_GameMenu
```

Relevant files and assets:

- `Source/LyraGame/UI/LyraHUDLayout.cpp`: `NativeOnInitialized` and `HandleEscapeAction`.
- `/Game/UI/HUD/W_HUDLayout`: `EscapeMenuClass` setting.
- `/Game/UI/W_GameMenu`: in-game menu UI.
- `/Game/ActionSet/LAS_Standard_HUD`: adds the gameplay HUD layout.

Changing `W_MainMenu` will not change the Escape menu, and changing `W_GameMenu` will not change the startup menu.

## Return To Main Menu

`UCommonGameInstance::ReturnToMainMenu` resets CommonUser and session state, then calls the engine return flow. Because `GameDefaultMap` is `L_Map_Default`, returning to the main menu reloads the frontend map and the default frontend Experience flow runs again.

Relevant implementation: `Plugins/CommonGame/Source/Private/CommonGameInstance.cpp`.

## Diagnostic Order

For a missing, incorrect, or stuck startup menu, check in this order:

1. Confirm the active map and the Experience reported by `LogLyraExperience`.
2. Check URL, PIE, command-line, and World Settings overrides before the config fallback.
3. Confirm `BP_Experience_Default` adds `BP_Frontend_State_Component` to `LyraGameState`.
4. Confirm `BP_UIPolicy` creates `W_UILayout_Overall` and that `UI.Layer.Menu` is registered.
5. Confirm `PressStartScreenClass` and `MainScreenClass` resolve to valid `UCommonActivatableWidget` classes.
6. If loading never clears, determine which `FrontendFlow` step is pending.
7. If only Escape fails, inspect `LAS_Standard_HUD`, `W_HUDLayout`, `UI.Action.Escape`, and `EscapeMenuClass` instead of the frontend component.

Prefer Unreal asset/Blueprint inspection tools when available. Without them, use config/source inspection and read-only `.uasset` reference searches; never edit `.uasset` binary data directly.
