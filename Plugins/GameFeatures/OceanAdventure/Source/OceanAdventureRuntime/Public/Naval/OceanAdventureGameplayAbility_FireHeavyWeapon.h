// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "Naval/OceanAdventureNavalTargetData.h"

#include "OceanAdventureGameplayAbility_FireHeavyWeapon.generated.h"

/**
 * Player-owned hold-to-charge action: hold to charge one shell, release to fire.
 *
 * This ability is granted to the player's Lyra ASC. The HeavyWeapon Actor is only a
 * SourceObject/validated world target; it does not host this ability or its input.
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

	virtual void OnGiveAbility(
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilitySpec& Spec) override;

	virtual void OnRemoveAbility(
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilitySpec& Spec) override;

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
	/** The HeavyWeapon Actor carried as this player-owned ability spec's SourceObject. */
	class ANavalHeavyWeaponActor* FindOperatedWeapon(
		FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo) const;

	class ANavalHeavyWeaponActor* FindOperatedWeapon() const;

	void BroadcastFailure(FGameplayTag FailReason, AActor* Station) const;

private:
	void OnTargetDataReadyCallback(const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag);
	void UpdateCharge(float DeltaTime);
	void UpdateTrajectoryPreview();
	void CommitChargedShot();
	void DestroyTrajectoryPreview();

	UFUNCTION()
	void OnInputReleased(float TimeHeld);

	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Naval|Cannon", meta = (ClampMin = "0.1", Units = "s"))
	float MaxChargeSeconds = 1.8f;

	/** Lowest preview/release charge; a short tap still produces a valid near-range shot. */
	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Naval|Cannon", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinimumChargeAlpha = 0.02f;

	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Naval|Cannon", meta = (ClampMin = "0.016", Units = "s"))
	float PreviewSampleInterval = 0.08f;

	FDelegateHandle OnTargetDataReadyHandle;

	UPROPERTY()
	TObjectPtr<class UOceanAdventureAbilityTask_NavalControl> ChargeTask;

	UPROPERTY()
	TObjectPtr<class AOceanAdventureCannonTrajectoryPreview> TrajectoryPreview;

	TWeakObjectPtr<class ANavalHeavyWeaponActor> ChargingWeapon;
	float ChargeElapsedSeconds = 0.0f;
	float CurrentChargeAlpha = 0.0f;
	bool bFireResolved = false;
	int32 LastLoggedChargeBucket = INDEX_NONE;
};
