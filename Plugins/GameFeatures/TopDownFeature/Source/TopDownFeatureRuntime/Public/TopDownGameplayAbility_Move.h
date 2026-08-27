// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "TopDownGameplayAbility_Move.generated.h"

/**
 * Wraps one held top-down direction in GAS.
 *
 * The AbilitySet grants this class four times, once per InputTag. That preserves diagonal
 * input while giving station/building modes one canonical movement ability to block.
 */
UCLASS()
class TOPDOWNFEATURERUNTIME_API UTopDownGameplayAbility_Move : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UTopDownGameplayAbility_Move(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	UFUNCTION()
	void OnInputReleased(float TimeHeld);

	void OnMovementStoppedTagChanged(const FGameplayTag ChangedTag, int32 NewCount);
	FGameplayTag ResolveDirectionTag(const FGameplayAbilitySpecHandle Handle) const;

	FGameplayTag ActiveDirectionTag;
	FDelegateHandle MovementStoppedDelegateHandle;
};
