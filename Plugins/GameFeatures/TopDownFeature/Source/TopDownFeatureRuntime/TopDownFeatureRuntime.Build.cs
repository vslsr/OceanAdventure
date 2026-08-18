// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TopDownFeatureRuntime : ModuleRules
{
	public TopDownFeatureRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"GameplayTags",
				"LyraGame",
				"ModularGameplay"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"EnhancedInput"
			}
		);
	}
}
