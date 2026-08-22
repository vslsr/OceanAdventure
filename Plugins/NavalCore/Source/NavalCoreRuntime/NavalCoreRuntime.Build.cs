// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NavalCoreRuntime : ModuleRules
{
	public NavalCoreRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// General framework layer. It may only reach Engine and other general plugins, never
		// LyraGame, GameplayAbilities, CommonUI or any GameFeature: cheats, save restore and
		// editor tooling call this API directly without going through GAS.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"BuildingCoreRuntime",
				"Core",
				"CoreUObject",
				"Engine",
				"GameplayTags",
				// Engine plugin, not a GameFeature: the reflection bridge that lets a host
				// feature's authoring script build its own AddComponents action.
				"GameFeatures",
				"NetCore"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Team resolution reads IGenericTeamAgentInterface so the framework can answer
				// "is this shot friendly?" without knowing anything about LyraGame's teams.
				"AIModule",
				"GameplayMessageRuntime",
				"PhysicsCore"
			}
		);
	}
}
