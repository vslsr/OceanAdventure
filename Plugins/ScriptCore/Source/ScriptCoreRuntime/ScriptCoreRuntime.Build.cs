// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class ScriptCoreRuntime : ModuleRules
{
	public ScriptCoreRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// General framework layer. It may only reach Engine and other general plugins, never
		// LyraGame, GameplayAbilities, CommonUI or any GameFeature: the script host has to come
		// up for a dedicated server, a cook commandlet and an editor tool alike, none of which
		// go through GAS.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Projects"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"DeveloperSettings",
				"Json"
			}
		);

		if (Target.bBuildEditor)
		{
			// Hot reload watches the script roots; the watcher only exists in editor builds.
			PrivateDependencyModuleNames.Add("DirectoryWatcher");
		}

		// PuerTS is supplied outside source control, the same way the EOS SDK is: it carries
		// prebuilt V8 binaries far too large for this repository. So the dependency is probed
		// rather than declared -- the project must still build, and still run its C++ gameplay,
		// on a checkout where nobody has installed it yet. Without it the module compiles
		// against the null backend and every script call becomes a loud no-op.
		bool bHasPuerts = FindPuertsJsEnvDirectory() != null;

		PrivateDefinitions.Add("WITH_SCRIPTCORE_PUERTS=" + (bHasPuerts ? "1" : "0"));

		if (bHasPuerts)
		{
			PrivateDependencyModuleNames.Add("JsEnv");
		}
	}

	/**
	 * Looks for the PuerTS JsEnv module under the project's and the engine's plugin folders.
	 *
	 * Both roots are derived, never spelled out: Target.ProjectFile knows where this checkout
	 * sits and Unreal.EngineDirectory knows where the engine sits, so the probe keeps working
	 * on a machine whose paths nobody here can know.
	 */
	private string FindPuertsJsEnvDirectory()
	{
		var Roots = new System.Collections.Generic.List<string>();

		if (Target.ProjectFile != null)
		{
			Roots.Add(Path.Combine(Target.ProjectFile.Directory.FullName, "Plugins"));
		}

		Roots.Add(Path.Combine(Unreal.EngineDirectory.FullName, "Plugins"));
		Roots.Add(Path.Combine(Unreal.EngineDirectory.FullName, "Plugins", "Marketplace"));

		foreach (string Root in Roots)
		{
			if (!Directory.Exists(Root))
			{
				continue;
			}

			// Puerts ships as Puerts/ in most releases and as PuerTS/ in some; a recursive
			// search for the module's own Build.cs matches either, and also matches a checkout
			// nested one level deeper (Plugins/ThirdParty/Puerts/...).
			foreach (string Candidate in Directory.GetFiles(Root, "JsEnv.Build.cs", SearchOption.AllDirectories))
			{
				return Path.GetDirectoryName(Candidate);
			}
		}

		return null;
	}
}
