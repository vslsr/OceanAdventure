// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OceanAdventureRuntime : ModuleRules
{
	public OceanAdventureRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The pawn remains independent of chunk streaming and camera implementations. This
		// module depends on OceanCoreRuntime only for the optional chunk presentation component
		// that OceanAdventure injects into AOceanChunkActor instances.
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
				"ModularGameplayActors",
				"NetCore",
				"OceanCoreRuntime",
				"ProceduralMeshComponent",
				// Build gameplay layer: the framework, the message bus the failure feedback
				// travels on, and the CommonUI stack that owns cursor/input policy.
				"BuildingCoreRuntime",
				// Naval framework: vessel state, the ballistic rule, heavy weapons. The
				// gameplay layer here owns the GAS abilities, input and match rules on top.
				"NavalCoreRuntime",
				// Carry framework: who is holding what. The gameplay layer here owns the
				// ability, the input tag and every game-specific rule about what may be lifted.
				"CarryCoreRuntime",
				"GameplayMessageRuntime",
				"CommonUI",
				"CommonGame",
				// UOceanAdventureNavalSettings derives from UDeveloperSettings in a public
				// header, and LyraGame only depends on this module privately.
				"DeveloperSettings",
				"UMG"
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
