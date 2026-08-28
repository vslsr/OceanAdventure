// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SubclassOf.h"

#include "OceanAdventureAssetLibrary.generated.h"

class AActor;
class UActorComponent;
class UGameFeatureAction;
class UInputMappingContext;
class UInputAction;
class ULyraAbilitySet;
class ULyraGameplayAbility;
class ULyraInputConfig;

/**
 * Reflection bridge for asset-authoring operations that Unreal Python cannot express
 * directly because the engine action's entry struct is not exported to Python.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates an Add Components action owned by Outer.
	 * ActorClasses and ComponentClasses are paired by index and must have equal lengths.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ocean Adventure|Assets")
	static UGameFeatureAction* CreateAddComponentsAction(
		UObject* Outer,
		const TArray<TSubclassOf<AActor>>& ActorClasses,
		const TArray<TSubclassOf<UActorComponent>>& ComponentClasses,
		bool bClientComponent = true,
		bool bServerComponent = true,
		FName ActionName = NAME_None);

	/**
	 * Creates an Add Input Context Mapping action owned by Outer.
	 * FInputMappingContextAndPriority is not a BlueprintType, so Python cannot build it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ocean Adventure|Assets")
	static UGameFeatureAction* CreateAddInputContextMappingAction(
		UObject* Outer,
		UInputMappingContext* InputMapping,
		int32 Priority = 0,
		FName ActionName = NAME_None);

	/**
	 * Rebuilds the helm mapping with signed Axis1D bindings (W/S throttle and D/A steer).
	 * Enhanced Input's negate modifier is an inline struct/object detail that is not reliably
	 * writable through the UE 5.7 Python wrapper, so this narrow bridge keeps asset authoring
	 * deterministic without moving input handling into runtime code.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ocean Adventure|Assets")
	static bool ConfigureHelmInputMapping(
		UInputMappingContext* InputMapping,
		UInputAction* ThrottleAction,
		UInputAction* SteerAction);

	/**
	 * Rebuilds the direct-planar mapping as one Axis2D action: A/D write X and W/S write Y.
	 * This remains an editor-only authoring bridge; runtime input is still tag-dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ocean Adventure|Assets")
	static bool ConfigureDirectHelmInputMapping(
		UInputMappingContext* InputMapping,
		UInputAction* DirectMoveAction);

	/** Creates an Add Input Binding action owned by Outer. */
	UFUNCTION(BlueprintCallable, Category = "Ocean Adventure|Assets")
	static UGameFeatureAction* CreateAddInputBindingAction(
		UObject* Outer,
		const TArray<ULyraInputConfig*>& InputConfigs,
		FName ActionName = NAME_None);

	/**
	 * Replaces an AbilitySet's gameplay-ability entries. Lyra marks the entry fields as
	 * EditDefaultsOnly, so UE 5.7 Python cannot construct or mutate the reflected structs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ocean Adventure|Assets")
	static bool ConfigureAbilitySetGameplayAbilities(
		ULyraAbilitySet* AbilitySet,
		const TArray<TSubclassOf<ULyraGameplayAbility>>& AbilityClasses,
		const TArray<int32>& AbilityLevels,
		const TArray<FGameplayTag>& InputTags);
};
