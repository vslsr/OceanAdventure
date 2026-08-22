// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "Naval/OceanAdventureNavalTargetData.h"

#include "OceanAdventureGameplayAbility_EmergencyRepair.generated.h"

class UNavalVesselComponent;

/**
 * 紧急抢修: the one chance to stop a hull at zero from becoming a wreck.
 *
 * A channel rather than an instant press, because design 8.7 wants the attacker to have a
 * counter-play -- keep shooting and the countdown shortens, board and the repair can be
 * interrupted. Available once per foundering life, so two crews cannot trade saves forever.
 *
 * The client channels; the server decides. It re-runs the same validation on commit, which is
 * what stops a player from repairing a ship they have since walked away from.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_EmergencyRepair : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_EmergencyRepair(
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

protected:
	/** The foundering vessel the avatar is standing on or next to. */
	UNavalVesselComponent* FindRepairableVessel() const;

	void BroadcastFailure(FGameplayTag FailReason, AActor* VesselActor) const;

	/** Fallback radius when the avatar is swimming next to the hull rather than standing on it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "100.0", Units = "cm"))
	float VesselSearchRadius = 900.0f;

private:
	UFUNCTION()
	void OnRepairChannelFinished();

	void OnTargetDataReadyCallback(const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag);

	TWeakObjectPtr<AActor> RepairTargetActor;
	FDelegateHandle OnTargetDataReadyHandle;
	bool bCommitSent = false;
};
