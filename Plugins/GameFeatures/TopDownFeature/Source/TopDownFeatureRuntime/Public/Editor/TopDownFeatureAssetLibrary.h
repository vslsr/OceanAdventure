// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SubclassOf.h"

#include "TopDownFeatureAssetLibrary.generated.h"

class AActor;
class UActorComponent;
class UGameFeatureAction;
class UInputMappingContext;
class ULyraAbilitySet;
class ULyraGameplayAbility;
class ULyraInputConfig;

/** Narrow authoring bridge for Lyra structs that UE 5.7 Python cannot construct safely. */
UCLASS()
class TOPDOWNFEATURERUNTIME_API UTopDownFeatureAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Top Down|Assets")
	static UGameFeatureAction* CreateAddComponentsAction(
		UObject* Outer,
		TSubclassOf<AActor> ActorClass,
		TSubclassOf<UActorComponent> ComponentClass,
		FName ActionName);

	UFUNCTION(BlueprintCallable, Category = "Top Down|Assets")
	static UGameFeatureAction* CreateAddInputContextMappingAction(
		UObject* Outer,
		UInputMappingContext* InputMapping,
		int32 Priority,
		FName ActionName);

	UFUNCTION(BlueprintCallable, Category = "Top Down|Assets")
	static UGameFeatureAction* CreateAddInputBindingAction(
		UObject* Outer,
		ULyraInputConfig* InputConfig,
		FName ActionName);

	UFUNCTION(BlueprintCallable, Category = "Top Down|Assets")
	static UGameFeatureAction* CreateAddAbilitiesAction(
		UObject* Outer,
		TSubclassOf<AActor> ActorClass,
		ULyraAbilitySet* AbilitySet,
		FName ActionName);

	UFUNCTION(BlueprintCallable, Category = "Top Down|Assets")
	static bool ConfigureAbilitySetGameplayAbilities(
		ULyraAbilitySet* AbilitySet,
		const TArray<TSubclassOf<ULyraGameplayAbility>>& AbilityClasses,
		const TArray<int32>& AbilityLevels,
		const TArray<FGameplayTag>& InputTags);
};
