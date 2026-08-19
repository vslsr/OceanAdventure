// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SubclassOf.h"

#include "OceanAdventureAssetLibrary.generated.h"

class AActor;
class UActorComponent;
class UGameFeatureAction;

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
		bool bServerComponent = true);
};
