// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OceanAdventureRuntime : ModuleRules
{
	public OceanAdventureRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// No dependency on OceanCoreRuntime or TopDownFeatureRuntime on purpose. The pawn
		// carries no chunk streaming or camera code of its own: both arrive as components
		// injected by the experience, so this module only needs the Lyra pawn framework.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"LyraGame",
				"ModularGameplay"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
