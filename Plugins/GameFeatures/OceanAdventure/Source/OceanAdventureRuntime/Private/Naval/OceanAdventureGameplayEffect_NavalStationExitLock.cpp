// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayEffect_NavalStationExitLock.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayEffect_NavalStationExitLock)

UOceanAdventureGameplayEffect_NavalStationExitLock::UOceanAdventureGameplayEffect_NavalStationExitLock(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(1.0f));
}
