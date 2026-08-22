// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Abilities/Tasks/AbilityTask.h"

#include "OceanAdventureAbilityTask_NavalControl.generated.h"

/**
 * Ticks while a player is at a station and hands the ability a control sample.
 *
 * A GameplayAbility does not tick, and steering is a held, continuous input rather than a
 * one-shot request, so it needs a task. The sample rate is deliberately well below frame rate:
 * a boat's throttle and a gun's traverse do not need 120 updates a second, and every sample
 * is a replicated target-data set.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureAbilityTask_NavalControl : public UAbilityTask
{
	GENERATED_BODY()

public:
	UOceanAdventureAbilityTask_NavalControl(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	static UOceanAdventureAbilityTask_NavalControl* NavalControlTick(
		UGameplayAbility* OwningAbility, float SampleInterval);

	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnNavalControlSample, float /*DeltaTime*/);
	FOnNavalControlSample OnControlSample;

private:
	float SampleInterval = 0.05f;
	float TimeSinceLastSample = 0.0f;
};
