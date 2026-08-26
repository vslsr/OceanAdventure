// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Naval/OceanAdventureGameplayAbility_NavalStation.h"

#include "OceanAdventureGameplayAbility_OperateHelm.generated.h"

class UNavalHelmComponent;

/**
 * 操作主舵台.
 *
 * While this runs, a priority-2 IMC_OceanHelm consumes W/A/S/D and the dedicated helm input
 * component captures signed throttle/steer values instead of footsteps. Pressing E again steps
 * off, pops the context, and the ship then holds its heading and coasts down rather than
 * stopping dead or steering itself, so a lone player can leave the wheel to fire or repair and
 * pay for it in drift.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_OperateHelm
	: public UOceanAdventureGameplayAbility_NavalStation
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_OperateHelm(
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
	virtual AActor* FindStationInRange() const override;
	virtual bool ServerOccupyStation(AActor* Station) override;
	virtual void ServerReleaseStation(AActor* Station) override;
	virtual void ServerApplyControl(AActor* Station, const FOceanAdventureNavalTargetData& Data) override;
	virtual bool BuildControlSample(FOceanAdventureNavalTargetData& OutData) const override;
	virtual FGameplayTag GetStationStatusTag() const override;
	virtual bool IsStationStillValid(AActor* Station) const override;
	virtual bool GetOperatorTransform(AActor* Station, FTransform& OutTransform) const override;

private:
	static UNavalHelmComponent* ResolveHelm(AActor* Station);
};
