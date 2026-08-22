// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/NavalCoreAssetLibrary.h"

#include "GameFeatureAction_AddComponents.h"
#include "NavalCoreRuntimeModule.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalCoreAssetLibrary)

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

UGameFeatureAction* UNavalCoreAssetLibrary::CreateAddComponentsAction(
	UObject* Outer,
	const TArray<TSubclassOf<AActor>>& ActorClasses,
	const TArray<TSubclassOf<UActorComponent>>& ComponentClasses,
	bool bClientComponent,
	bool bServerComponent,
	FName ActionName)
{
	if (!IsValid(Outer))
	{
		UE_LOG(LogNavalCore, Error, TEXT("Cannot create Add Components action without a valid outer"));
		return nullptr;
	}

	if (ActorClasses.Num() != ComponentClasses.Num())
	{
		UE_LOG(
			LogNavalCore,
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
			UE_LOG(LogNavalCore, Error, TEXT("Cannot create Add Components action: entry %d has a null class"), Index);
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
