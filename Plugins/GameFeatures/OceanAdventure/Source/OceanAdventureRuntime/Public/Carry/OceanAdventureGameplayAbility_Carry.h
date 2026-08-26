// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "Carry/OceanAdventureCarryTargetData.h"

#include "OceanAdventureGameplayAbility_Carry.generated.h"

class UCarrierComponent;
class UCarryableComponent;

/**
 * 抬起 / 放下: one key, both directions.
 *
 * Design 7.10 is specific that a field gun arrives as a kit somebody carried there, and that
 * carrying it is the cost -- the carrier is slowed, cannot shoot, and can be robbed of it.
 * That is why this is a world attachment and not an inventory slot: a backpack would delete
 * all three at once, and the gun's durability, team and reload state would have to be
 * serialised into an item just to survive the trip.
 *
 * V1 is the attachment itself: lift, hold at the carry point with no collision, put back
 * down in front of the carrier. The movement penalty, the pack/unpack windup, the ghost
 * preview and the ground legality check on put-down are the next version.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_Carry : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_Carry(
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
	/**
	 * The game's own rules about what may be lifted, on top of the framework's. Run
	 * identically on the requesting client and on the server, so the two cannot disagree.
	 */
	bool CanCarryTarget(const UCarryableComponent* Carryable, FGameplayTag& OutFailReason) const;

	void BroadcastFailure(FGameplayTag FailReason, AActor* CarryTarget) const;

private:
	UCarrierComponent* GetCarrierComponent() const;

	void OnTargetDataReadyCallback(const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag);

	FDelegateHandle OnTargetDataReadyHandle;
	bool bCarryResolved = false;
};
