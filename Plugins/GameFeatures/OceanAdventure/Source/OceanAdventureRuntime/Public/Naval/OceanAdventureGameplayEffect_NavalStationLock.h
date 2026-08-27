// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffect.h"

#include "OceanAdventureGameplayEffect_NavalStationLock.generated.h"

/**
 * Infinite carrier effect for the tags owned by an active naval station ability.
 *
 * The ability adds MovementStopped and its concrete station status to DynamicGrantedTags on
 * the outgoing spec, then removes the returned active-effect handle on every exit path.
 */
UCLASS(NotBlueprintable)
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayEffect_NavalStationLock
	: public UGameplayEffect
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayEffect_NavalStationLock(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
