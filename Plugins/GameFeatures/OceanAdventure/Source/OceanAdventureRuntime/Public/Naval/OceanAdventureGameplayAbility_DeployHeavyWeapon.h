// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "Naval/OceanAdventureNavalTargetData.h"

#include "OceanAdventureGameplayAbility_DeployHeavyWeapon.generated.h"

/**
 * 地面直接架设重武器.
 *
 * Design 7.10 removes the foundation and the war banner: a field gun's own legs are what let
 * it stand anywhere the ground can carry the whole footprint. What it does not remove is the
 * bet -- setting up in the open is visible, the half-built gun cannot fire, and a light
 * weapon that reaches the gunner first wins the exchange.
 *
 * The team-level cap is a world-entity budget, not a building rule: it exists so an island
 * cannot end up carpeted in turrets.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_DeployHeavyWeapon : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_DeployHeavyWeapon(
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
	/** Ground, team budget and a configured class, checked identically on both sides. */
	bool CanDeployHere(const FVector& Location, FGameplayTag& OutFailReason) const;

	FVector ComputeDeployLocation() const;

	void BroadcastFailure(FGameplayTag FailReason) const;

	/** How far in front of the player the legs go down. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "100.0", Units = "cm"))
	float DeployForwardDistance = 260.0f;

private:
	void OnTargetDataReadyCallback(const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag);

	/** Counts this team's field guns without walking the world. */
	int32 CountTeamGroundWeapons(int32 TeamId) const;

	FDelegateHandle OnTargetDataReadyHandle;
	bool bDeployResolved = false;
};
