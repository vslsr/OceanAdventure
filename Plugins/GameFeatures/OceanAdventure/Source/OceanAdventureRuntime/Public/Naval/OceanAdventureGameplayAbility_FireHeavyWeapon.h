// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "Naval/OceanAdventureNavalTargetData.h"

#include "OceanAdventureGameplayAbility_FireHeavyWeapon.generated.h"

/**
 * One press, one shell.
 *
 * Split from the operate ability so that holding the trigger cannot become a stream and so
 * that every refusal -- reloading, minimum range, a wall in front of the muzzle -- is a
 * single, readable failure the player gets told about once.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_FireHeavyWeapon : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_FireHeavyWeapon(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;

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
	/** The gun this pawn is currently standing at, found from its attachment. */
	class ANavalHeavyWeaponActor* FindOperatedWeapon() const;

	void BroadcastFailure(FGameplayTag FailReason, AActor* Station) const;

private:
	void OnTargetDataReadyCallback(const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag);

	FDelegateHandle OnTargetDataReadyHandle;
	bool bFireResolved = false;
};
