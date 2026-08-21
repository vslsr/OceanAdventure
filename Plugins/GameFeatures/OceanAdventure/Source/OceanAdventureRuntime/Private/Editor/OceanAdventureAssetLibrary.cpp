// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/OceanAdventureAssetLibrary.h"

#include "GameFeatureAction_AddComponents.h"

namespace
{
	/**
	 * Re-running an authoring script must replace the action it created last time instead of
	 * appending a second copy, so callers name their actions and any previous holder of that
	 * name is moved aside first.
	 */
	void ClearExistingActionName(UObject* Outer, FName ActionName)
	{
		if (ActionName.IsNone())
		{
			return;
		}

		if (UObject* Existing = StaticFindObjectFast(UObject::StaticClass(), Outer, ActionName))
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
		}
	}
}

#include "GameFeatures/GameFeatureAction_AddInputBinding.h"
#include "GameFeatures/GameFeatureAction_AddInputContextMapping.h"
#include "Input/LyraInputConfig.h"
#include "InputMappingContext.h"
#include "OceanAdventureRuntimeModule.h"

UGameFeatureAction* UOceanAdventureAssetLibrary::CreateAddComponentsAction(
	UObject* Outer,
	const TArray<TSubclassOf<AActor>>& ActorClasses,
	const TArray<TSubclassOf<UActorComponent>>& ComponentClasses,
	bool bClientComponent,
	bool bServerComponent,
	FName ActionName)
{
	if (!IsValid(Outer))
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("Cannot create Add Components action without a valid outer"));
		return nullptr;
	}

	if (ActorClasses.Num() != ComponentClasses.Num())
	{
		UE_LOG(
			LogOceanAdventure,
			Error,
			TEXT("Cannot create Add Components action: %d actor classes but %d component classes"),
			ActorClasses.Num(),
			ComponentClasses.Num());
		return nullptr;
	}

	ClearExistingActionName(Outer, ActionName);
	UGameFeatureAction_AddComponents* Action = NewObject<UGameFeatureAction_AddComponents>(Outer, ActionName);
	for (int32 Index = 0; Index < ActorClasses.Num(); ++Index)
	{
		if (!ActorClasses[Index] || !ComponentClasses[Index])
		{
			UE_LOG(LogOceanAdventure, Error, TEXT("Cannot create Add Components action: entry %d has a null class"), Index);
			return nullptr;
		}

		FGameFeatureComponentEntry& Entry = Action->ComponentList.AddDefaulted_GetRef();
		Entry.ActorClass = ActorClasses[Index].Get();
		Entry.ComponentClass = ComponentClasses[Index].Get();
		Entry.bClientComponent = bClientComponent;
		Entry.bServerComponent = bServerComponent;
	}

	return Action;
}

UGameFeatureAction* UOceanAdventureAssetLibrary::CreateAddInputContextMappingAction(
	UObject* Outer,
	UInputMappingContext* InputMapping,
	int32 Priority,
	FName ActionName)
{
	if (!IsValid(Outer) || !IsValid(InputMapping))
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("Cannot create Add Input Context Mapping action without a valid outer and mapping"));
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

UGameFeatureAction* UOceanAdventureAssetLibrary::CreateAddInputBindingAction(
	UObject* Outer,
	const TArray<ULyraInputConfig*>& InputConfigs,
	FName ActionName)
{
	if (!IsValid(Outer) || InputConfigs.IsEmpty())
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("Cannot create Add Input Binding action without a valid outer and configs"));
		return nullptr;
	}

	ClearExistingActionName(Outer, ActionName);
	UGameFeatureAction_AddInputBinding* Action = NewObject<UGameFeatureAction_AddInputBinding>(Outer, ActionName);
	for (const ULyraInputConfig* Config : InputConfigs)
	{
		if (Config)
		{
			Action->InputConfigs.Add(Config);
		}
	}
	return Action;
}
