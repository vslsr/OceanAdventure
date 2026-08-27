// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffect.h"

#include "OceanAdventureGameplayEffect_NavalStationExitLock.generated.h"

/**
 * Duration carrier effect for Status.Naval.StationExitLock.
 *
 * The station ability sets the exact duration and adds the tag to DynamicGrantedTags on the
 * outgoing spec. Overlapping applications intentionally expire independently, keeping the
 * exit lock active until the newest application ends.
 */
UCLASS(NotBlueprintable)
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayEffect_NavalStationExitLock
	: public UGameplayEffect
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayEffect_NavalStationExitLock(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
