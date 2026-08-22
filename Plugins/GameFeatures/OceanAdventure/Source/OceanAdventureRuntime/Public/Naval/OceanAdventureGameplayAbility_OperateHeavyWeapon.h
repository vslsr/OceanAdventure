// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Naval/OceanAdventureGameplayAbility_NavalStation.h"

#include "OceanAdventureGameplayAbility_OperateHeavyWeapon.generated.h"

class ANavalHeavyWeaponActor;

/**
 * Standing at a heavy weapon and traversing it.
 *
 * Occupying is what costs the player their light weapon and their mobility, which is the
 * whole trade the design asks for: strong output against hulls, walls and choke points, in
 * exchange for being a stationary, visible target that an attacker can close on. Firing is a
 * separate ability so that one press is one shell.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_OperateHeavyWeapon
	: public UOceanAdventureGameplayAbility_NavalStation
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_OperateHeavyWeapon(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual AActor* FindStationInRange() const override;
	virtual bool ServerOccupyStation(AActor* Station) override;
	virtual void ServerReleaseStation(AActor* Station) override;
	virtual void ServerApplyControl(AActor* Station, const FOceanAdventureNavalTargetData& Data) override;
	virtual bool BuildControlSample(FOceanAdventureNavalTargetData& OutData) const override;
	virtual FGameplayTag GetStationStatusTag() const override;
	virtual bool IsStationStillValid(AActor* Station) const override;
};
