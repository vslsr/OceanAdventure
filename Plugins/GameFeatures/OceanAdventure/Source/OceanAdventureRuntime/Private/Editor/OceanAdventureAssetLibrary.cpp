// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/OceanAdventureAssetLibrary.h"

#include "GameFeatureAction_AddComponents.h"
#include "OceanAdventureRuntimeModule.h"

UGameFeatureAction* UOceanAdventureAssetLibrary::CreateAddComponentsAction(
	UObject* Outer,
	const TArray<TSubclassOf<AActor>>& ActorClasses,
	const TArray<TSubclassOf<UActorComponent>>& ComponentClasses,
	bool bClientComponent,
	bool bServerComponent)
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

	UGameFeatureAction_AddComponents* Action = NewObject<UGameFeatureAction_AddComponents>(Outer);
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
