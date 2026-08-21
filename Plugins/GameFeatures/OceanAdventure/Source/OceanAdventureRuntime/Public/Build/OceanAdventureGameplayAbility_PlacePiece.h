// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "Build/OceanAdventureBuildTargetData.h"

#include "OceanAdventureGameplayAbility_PlacePiece.generated.h"

class UBuildPreviewComponent;
class UBuildStructureComponent;

/**
 * Places the selected piece where the local ghost is standing.
 *
 * Same shape as ULyraGameplayAbility_RangedWeapon: the client resolves the target locally,
 * ships it through GAS' target-data channel, and the server re-validates before applying.
 * The target data is a request, never an authorisation.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_PlacePiece : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_PlacePiece(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

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
	/** Server side. Overridden by the removal ability. */
	virtual bool ApplyToStructure(
		UBuildStructureComponent* Structure,
		const FOceanAdventureBuildTargetData& Data,
		FGameplayTag& OutFailReason);

	/** Client side gate before anything is sent. */
	virtual bool BuildLocalTargetData(FOceanAdventureBuildTargetData& OutData, FGameplayTag& OutFailReason) const;

	UBuildPreviewComponent* GetPreviewComponent() const;
	void BroadcastFailure(FGameplayTag FailReason, AActor* HostActor) const;

private:
	void OnTargetDataReadyCallback(const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag);

	FDelegateHandle OnTargetDataReadyHandle;
};
