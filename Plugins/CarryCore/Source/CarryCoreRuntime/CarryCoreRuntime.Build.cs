// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CarryCoreRuntime : ModuleRules
{
	public CarryCoreRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// General framework layer. It may only reach Engine and other general plugins, never
		// LyraGame, GameplayAbilities, CommonUI or any GameFeature: cheats, save restore and
		// editor tooling put things down and pick them up without going through GAS.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"GameplayTags",
				"NetCore"
			}
		);
	}
}
