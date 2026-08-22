// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureAbilityTask_NavalControl.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureAbilityTask_NavalControl)

UOceanAdventureAbilityTask_NavalControl::UOceanAdventureAbilityTask_NavalControl(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bTickingTask = true;
}

UOceanAdventureAbilityTask_NavalControl* UOceanAdventureAbilityTask_NavalControl::NavalControlTick(
	UGameplayAbility* OwningAbility, float SampleInterval)
{
	UOceanAdventureAbilityTask_NavalControl* Task = NewAbilityTask<UOceanAdventureAbilityTask_NavalControl>(OwningAbility);
	Task->SampleInterval = FMath::Max(0.01f, SampleInterval);
	return Task;
}

void UOceanAdventureAbilityTask_NavalControl::Activate()
{
	Super::Activate();

	// Fire once immediately so a tap on the throttle is not swallowed by the first interval.
	TimeSinceLastSample = SampleInterval;
}

void UOceanAdventureAbilityTask_NavalControl::TickTask(float DeltaTime)
{
	Super::TickTask(DeltaTime);

	TimeSinceLastSample += DeltaTime;
	if (TimeSinceLastSample < SampleInterval)
	{
		return;
	}

	const float SampleDelta = TimeSinceLastSample;
	TimeSinceLastSample = 0.0f;
	OnControlSample.Broadcast(SampleDelta);
}
