// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "Naval/OceanAdventureNavalTargetData.h"
#include "UObject/SoftObjectPtr.h"

#include "OceanAdventureGameplayAbility_DriveHelm.generated.h"

class UInputAction;
class UInputMappingContext;
class ULyraInputConfig;
class UOceanAdventureAbilityTask_NavalControl;

/**
 * Temporary player-ASC ability granted only while OperateHelm owns a valid station.
 *
 * Its spec carries both helm InputTags and the station as SourceObject. The owning client
 * samples the tagged Enhanced Input actions; the server revalidates the station/operator and
 * is the only side that writes UNavalHelmComponent control intent.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_DriveHelm
	: public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_DriveHelm(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

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

	virtual void OnRemoveAbility(
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilitySpec& Spec) override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Naval|Helm Input")
	TSoftObjectPtr<ULyraInputConfig> DriveInputConfig;

	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Naval|Helm Input")
	TSoftObjectPtr<UInputMappingContext> HelmMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Naval|Helm Input", meta = (ClampMin = "0"))
	int32 HelmMappingPriority = 2;

	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Naval|Helm Input", meta = (ClampMin = "0.01", Units = "s"))
	float ControlSampleInterval = 0.05f;

private:
	AActor* FindSourceStation(
		FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo) const;
	bool StartLocalInput();
	void StopLocalInput();
	void SampleAndSendControl(float DeltaTime);
	void SendControlRequest(const FOceanAdventureNavalTargetData& Data);
	void OnTargetDataReadyCallback(
		const FGameplayAbilityTargetDataHandle& InData,
		FGameplayTag ApplicationTag);

	TWeakObjectPtr<AActor> SourceStation;

	UPROPERTY(Transient)
	TObjectPtr<const UInputAction> ThrottleAction;

	UPROPERTY(Transient)
	TObjectPtr<const UInputAction> SteerAction;

	UPROPERTY()
	TObjectPtr<UOceanAdventureAbilityTask_NavalControl> ControlTask;

	FDelegateHandle OnTargetDataReadyHandle;
	bool bMappingPushed = false;
};
