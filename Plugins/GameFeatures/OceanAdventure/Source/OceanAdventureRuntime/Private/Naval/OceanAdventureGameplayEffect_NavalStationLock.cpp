// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayEffect_NavalStationLock.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayEffect_NavalStationLock)

UOceanAdventureGameplayEffect_NavalStationLock::UOceanAdventureGameplayEffect_NavalStationLock(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
}
