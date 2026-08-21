// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Build/OceanAdventureGameplayAbility_PlacePiece.h"

#include "OceanAdventureGameplayAbility_RemovePiece.generated.h"

/** Same target-data pipeline as placement; the server calls TryRemovePiece instead. */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_RemovePiece
	: public UOceanAdventureGameplayAbility_PlacePiece
{
	GENERATED_BODY()

protected:
	virtual bool ApplyToStructure(
		UBuildStructureComponent* Structure,
		const FOceanAdventureBuildTargetData& Data,
		FGameplayTag& OutFailReason) override;

	virtual bool BuildLocalTargetData(
		FOceanAdventureBuildTargetData& OutData,
		FGameplayTag& OutFailReason) const override;
};
