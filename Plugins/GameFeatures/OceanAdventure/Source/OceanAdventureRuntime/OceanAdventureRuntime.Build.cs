// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OceanAdventureRuntime : ModuleRules
{
	public OceanAdventureRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// No dependency on OceanCoreRuntime or TopDownFeatureRuntime on purpose. The pawn
		// carries no chunk streaming or camera code of its own: both arrive as components
		// injected by the experience, so its gameplay code only needs the Lyra pawn framework.
		// GameFeatures is used by the narrow reflected asset factory that lets the editor
		// Python script build an AddComponents action without depending on feature classes.
		//
		// ALyraCharacter implements IGenericTeamAgentInterface (AIModule), IGameplayCueInterface
		// and IAbilitySystemInterface (GameplayAbilities), and IGameplayTagAssetInterface
		// (GameplayTags). Deriving from it emits those interface vtables into this module, so
		// their modules have to be on our link line -- inheriting them through LyraGame's
		// public dependencies is not enough. Lyra's own ShooterCore declares them the same way.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AIModule",
				"GameplayAbilities",
				"GameplayTags",
				"GameplayTasks",
				"GameFeatures",
				"LyraGame",
				"ModularGameplay",
				"ModularGameplayActors"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
