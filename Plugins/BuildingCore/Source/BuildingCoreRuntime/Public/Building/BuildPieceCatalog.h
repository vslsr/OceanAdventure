// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "BuildPieceCatalog.generated.h"

class UBuildPieceDefinition;

/** Stable, append-only network index for the pieces supported by one host family. */
UCLASS(BlueprintType, Const)
class BUILDINGCORERUNTIME_API UBuildPieceCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building")
	TArray<TObjectPtr<UBuildPieceDefinition>> Pieces;

	UFUNCTION(BlueprintPure, Category = "Building")
	const UBuildPieceDefinition* GetByIndex(int32 Index) const;

	bool FindIndex(const UBuildPieceDefinition* Definition, uint16& OutIndex) const;
	const UBuildPieceDefinition* FindByTag(FGameplayTag Tag) const;
};
