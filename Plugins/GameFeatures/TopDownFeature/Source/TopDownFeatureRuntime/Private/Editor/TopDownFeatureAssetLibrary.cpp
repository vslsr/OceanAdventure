// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/TopDownFeatureAssetLibrary.h"

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "AbilitySystem/LyraAbilitySet.h"
#include "GameFeatureAction_AddComponents.h"
#include "GameFeatures/GameFeatureAction_AddAbilities.h"
#include "GameFeatures/GameFeatureAction_AddInputBinding.h"
#include "GameFeatures/GameFeatureAction_AddInputContextMapping.h"
#include "Input/LyraInputConfig.h"
#include "InputMappingContext.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TopDownFeatureAssetLibrary)

DEFINE_LOG_CATEGORY_STATIC(LogTopDownFeatureAssets, Log, All);

namespace
{
	void ClearExistingActionName(UObject* Outer, FName ActionName)
	{
		if (Outer && !ActionName.IsNone())
		{
			if (UObject* Existing = StaticFindObjectFast(UObject::StaticClass(), Outer, ActionName))
			{
				Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
			}
		}
	}
}

UGameFeatureAction* UTopDownFeatureAssetLibrary::CreateAddComponentsAction(
	UObject* Outer,
	TSubclassOf<AActor> ActorClass,
	TSubclassOf<UActorComponent> ComponentClass,
	FName ActionName)
{
	if (!IsValid(Outer) || !ActorClass || !ComponentClass)
	{
		return nullptr;
	}
	ClearExistingActionName(Outer, ActionName);
	UGameFeatureAction_AddComponents* Action =
		NewObject<UGameFeatureAction_AddComponents>(Outer, ActionName);
	FGameFeatureComponentEntry& Entry = Action->ComponentList.AddDefaulted_GetRef();
	Entry.ActorClass = ActorClass.Get();
	Entry.ComponentClass = ComponentClass.Get();
	Entry.bClientComponent = true;
	Entry.bServerComponent = true;
	return Action;
}

UGameFeatureAction* UTopDownFeatureAssetLibrary::CreateAddInputContextMappingAction(
	UObject* Outer,
	UInputMappingContext* InputMapping,
	int32 Priority,
	FName ActionName)
{
	if (!IsValid(Outer) || !IsValid(InputMapping))
	{
		return nullptr;
	}
	ClearExistingActionName(Outer, ActionName);
	UGameFeatureAction_AddInputContextMapping* Action =
		NewObject<UGameFeatureAction_AddInputContextMapping>(Outer, ActionName);
	FInputMappingContextAndPriority& Entry = Action->InputMappings.AddDefaulted_GetRef();
	Entry.InputMapping = InputMapping;
	Entry.Priority = Priority;
	return Action;
}

UGameFeatureAction* UTopDownFeatureAssetLibrary::CreateAddInputBindingAction(
	UObject* Outer,
	ULyraInputConfig* InputConfig,
	FName ActionName)
{
	if (!IsValid(Outer) || !IsValid(InputConfig))
	{
		return nullptr;
	}
	ClearExistingActionName(Outer, ActionName);
	UGameFeatureAction_AddInputBinding* Action =
		NewObject<UGameFeatureAction_AddInputBinding>(Outer, ActionName);
	Action->InputConfigs.Add(InputConfig);
	return Action;
}

UGameFeatureAction* UTopDownFeatureAssetLibrary::CreateAddAbilitiesAction(
	UObject* Outer,
	TSubclassOf<AActor> ActorClass,
	ULyraAbilitySet* AbilitySet,
	FName ActionName)
{
	if (!IsValid(Outer) || !ActorClass || !IsValid(AbilitySet))
	{
		return nullptr;
	}
	ClearExistingActionName(Outer, ActionName);
	UGameFeatureAction_AddAbilities* Action =
		NewObject<UGameFeatureAction_AddAbilities>(Outer, ActionName);
	FGameFeatureAbilitiesEntry& Entry = Action->AbilitiesList.AddDefaulted_GetRef();
	Entry.ActorClass = ActorClass.Get();
	Entry.GrantedAbilitySets.Add(AbilitySet);
	return Action;
}

bool UTopDownFeatureAssetLibrary::ConfigureAbilitySetGameplayAbilities(
	ULyraAbilitySet* AbilitySet,
	const TArray<TSubclassOf<ULyraGameplayAbility>>& AbilityClasses,
	const TArray<int32>& AbilityLevels,
	const TArray<FGameplayTag>& InputTags)
{
	if (!IsValid(AbilitySet)
		|| AbilityClasses.Num() != AbilityLevels.Num()
		|| AbilityClasses.Num() != InputTags.Num())
	{
		return false;
	}

	FArrayProperty* GrantedAbilitiesProperty = FindFProperty<FArrayProperty>(
		ULyraAbilitySet::StaticClass(), TEXT("GrantedGameplayAbilities"));
	const FStructProperty* EntryProperty = GrantedAbilitiesProperty
		? CastField<FStructProperty>(GrantedAbilitiesProperty->Inner)
		: nullptr;
	if (!GrantedAbilitiesProperty
		|| !EntryProperty
		|| EntryProperty->Struct != FLyraAbilitySet_GameplayAbility::StaticStruct())
	{
		UE_LOG(LogTopDownFeatureAssets, Error, TEXT("Lyra AbilitySet reflection layout changed"));
		return false;
	}

	for (int32 Index = 0; Index < AbilityClasses.Num(); ++Index)
	{
		if (!AbilityClasses[Index] || AbilityLevels[Index] < 1 || !InputTags[Index].IsValid())
		{
			return false;
		}
	}

	AbilitySet->Modify();
	void* ArrayValue = GrantedAbilitiesProperty->ContainerPtrToValuePtr<void>(AbilitySet);
	FScriptArrayHelper ArrayHelper(GrantedAbilitiesProperty, ArrayValue);
	ArrayHelper.EmptyValues(AbilityClasses.Num());
	for (int32 Index = 0; Index < AbilityClasses.Num(); ++Index)
	{
		const int32 EntryIndex = ArrayHelper.AddValue();
		FLyraAbilitySet_GameplayAbility* Entry =
			reinterpret_cast<FLyraAbilitySet_GameplayAbility*>(ArrayHelper.GetRawPtr(EntryIndex));
		Entry->Ability = AbilityClasses[Index];
		Entry->AbilityLevel = AbilityLevels[Index];
		Entry->InputTag = InputTags[Index];
	}
	AbilitySet->MarkPackageDirty();
	return true;
}
