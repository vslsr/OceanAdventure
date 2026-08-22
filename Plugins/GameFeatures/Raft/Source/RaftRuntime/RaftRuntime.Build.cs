// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RaftRuntime : ModuleRules
{
	public RaftRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"BuildingCoreRuntime",
				"Core",
				"CoreUObject",
				"Engine",
				// Framework layer only: the raft implements INavalBuoyancyControl so the vessel
				// state machine can stop a wreck floating without knowing how a raft floats.
				"NavalCoreRuntime"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"GameplayTags",
				"OceanCoreRuntime",
				"Projects"
			}
		);
	}
}
