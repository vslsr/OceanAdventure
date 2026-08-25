// Copyright 2026 Nwiro. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class NwiroIntegrationKit : ModuleRules
{
	public NwiroIntegrationKit(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = true;
		MinSourceFilesForUnityBuildOverride = 1;

		ShadowVariableWarningLevel = WarningLevel.Off;

		// Bundled miniz for zip extraction
		PublicIncludePaths.AddRange(new string[] {
			Path.Combine(ModuleDirectory, "../ThirdParty/miniz")
		});

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"LevelEditor",
				"ToolMenus",
				"Projects",
				"Json",
				"JsonUtilities",
				"UnrealEd",
				"AssetTools",
				"BlueprintGraph",
				"KismetCompiler",
				"Kismet",
				"GraphEditor",
				"InputBlueprintNodes",
				"EnhancedInput",
				"PythonScriptPlugin",
				"EngineSettings",
				"Networking",
				"Sockets",
				"HTTP",
				"HTTPServer",
				"MaterialEditor",
				"EditorScriptingUtilities",
				"WebBrowser",
				"ApplicationCore",
				"DesktopPlatform",
				"ImageWrapper",
				"RenderCore",
				"LevelSequence",
				"MovieScene",
				"MovieSceneTracks",
				"AIModule",
				"UMG",
				"UMGEditor",
				"Niagara",
				"GameplayTasks",
				"NavigationSystem",
				"StateTreeModule",
				"StateTreeEditorModule",
				"IKRig",
				"IKRigEditor",
				"PoseSearch",
				"PoseSearchEditor",
				"Landscape",
				"LandscapeEditor",
				"Foliage",
				"PhysicsCore",
				"EditorSubsystem",
				"PCG",
				"ContentBrowser",
				"AssetRegistry"
			}
		);

		// Link imm32.lib for Win64 — used by the IME message-forwarding
		// bridge (NwiroIKImeBridge) which routes Windows WM_IME_* messages
		// from Slate's intercepting message pump to the embedded CEF browser
		// HWND. The current bridge implementation only calls user32 APIs
		// (PostMessageW/SendMessageW/EnumChildWindows), so imm32 isn't
		// strictly required yet — linking it ahead allows future use of
		// ImmGetContext/ImmAssociateContextEx for explicit HIMC management
		// without another Build.cs round-trip.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("imm32.lib");
		}

		// macOS IME path is handled entirely by the cross-platform
		// SWebBrowser::BindInputMethodSystem() call in NwiroIKPanel.cpp —
		// no .mm files, no Cocoa interop, no bEnableObjCExceptions needed.
	}
}
