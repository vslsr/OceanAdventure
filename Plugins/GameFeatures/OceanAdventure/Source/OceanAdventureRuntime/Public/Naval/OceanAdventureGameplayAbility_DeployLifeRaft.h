// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "Naval/OceanAdventureNavalTargetData.h"

#include "OceanAdventureGameplayAbility_DeployLifeRaft.generated.h"

class UOceanAdventureNavalMatchComponent;

/**
 * 部署折叠救生筏 -- the guarantee that losing your ship never removes you from the match.
 *
 * Design 8.8 is a rule about participation, not compensation: a team with a surviving member
 * must always be able to move on the water again, and the thing they get must be clearly
 * worse than a real ship so that scuttling a damaged one is never the smart play. It is
 * available once per team, only while that team has no working vessel, and the deploy is a
 * channel an attacker can interrupt.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_DeployLifeRaft : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_DeployLifeRaft(
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
	/** Same predicate on both sides: no working vessel, entitlement unspent, deck-free ground. */
	bool CanDeployLifeRaft(FGameplayTag& OutFailReason) const;

	UOceanAdventureNavalMatchComponent* FindMatchComponent() const;

	/** Where the raft is put down: just ahead of the player, on the water. */
	FVector ComputeDeployLocation() const;

	void BroadcastFailure(FGameplayTag FailReason) const;

	/** How far ahead of the player the raft appears. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "100.0", Units = "cm"))
	float DeployForwardDistance = 400.0f;

private:
	UFUNCTION()
	void OnDeployChannelFinished();

	void OnTargetDataReadyCallback(const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag);

	/** Server side. Spawns the hull and scales it down to a life raft. */
	bool SpawnLifeRaft(const FVector& DeployLocation);

	FDelegateHandle OnTargetDataReadyHandle;
	FVector PendingDeployLocation = FVector::ZeroVector;
	bool bCommitSent = false;
};
