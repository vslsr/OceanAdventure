// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Naval/OceanAdventureGameplayAbility_NavalStation.h"

#include "OceanAdventureGameplayAbility_OperateHelm.generated.h"

class UNavalHelmComponent;

/**
 * 操作主舵台.
 *
 * This ability owns station occupancy and the attached-character state. On server acceptance it
 * temporarily grants DriveHelm to the same player ASC; that second ability owns the tagged W/A/S/D
 * input and TargetData control stream. Pressing E again revokes it before restoring walking.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_OperateHelm
	: public UOceanAdventureGameplayAbility_NavalStation
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_OperateHelm(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

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
	virtual FGameplayTag GetStationStatusTag() const override;
	virtual bool IsStationStillValid(AActor* Station) const override;
	virtual bool GetOperatorTransform(AActor* Station, FTransform& OutTransform) const override;

private:
	static UNavalHelmComponent* ResolveHelm(AActor* Station);
	bool GrantDriveAbility(AActor* Station);
	void RevokeDriveAbility();

	FGameplayAbilitySpecHandle GrantedDriveAbilityHandle;
};
