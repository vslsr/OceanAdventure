// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SubclassOf.h"

#include "NavalCoreAssetLibrary.generated.h"

class AActor;
class UActorComponent;
class UGameFeatureAction;

/**
 * Reflection bridge for asset-authoring steps Unreal Python cannot express directly,
 * because the engine action's entry struct is not exported to Python.
 *
 * It lives in the framework layer rather than in a feature so that a host feature -- the
 * raft today, another hull later -- can build its own component-injection action without
 * calling into another GameFeature's editor scripts.
 */
UCLASS()
class NAVALCORERUNTIME_API UNavalCoreAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates an Add Components action owned by Outer.
	 * ActorClasses and ComponentClasses are paired by index and must have equal lengths.
	 */
	UFUNCTION(BlueprintCallable, Category = "Naval|Assets")
	static UGameFeatureAction* CreateAddComponentsAction(
		UObject* Outer,
		const TArray<TSubclassOf<AActor>>& ActorClasses,
		const TArray<TSubclassOf<UActorComponent>>& ComponentClasses,
		bool bClientComponent = true,
		bool bServerComponent = true,
		FName ActionName = NAME_None);
};
