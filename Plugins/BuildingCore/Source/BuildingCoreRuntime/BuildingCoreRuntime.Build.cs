// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BuildingCoreRuntime : ModuleRules
{
	public BuildingCoreRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

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

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"InputCore"
			}
		);
	}
}
