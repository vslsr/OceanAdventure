// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Naval/OceanAdventureGameplayAbility_NavalStation.h"

#include "OceanAdventureGameplayAbility_OperateHelm.generated.h"

class UNavalHelmComponent;

/**
 * 操作主舵台.
 *
 * While this runs, the character's movement input becomes throttle and steering instead of
 * footsteps -- the design's "移动输入转换为油门与转向". Pressing the same key again steps off,
 * and the ship then holds its heading and coasts down rather than stopping dead or steering
 * itself, so a lone player can leave the wheel to fire or repair and pay for it in drift.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_OperateHelm
	: public UOceanAdventureGameplayAbility_NavalStation
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_OperateHelm(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual AActor* FindStationInRange() const override;
	virtual bool ServerOccupyStation(AActor* Station) override;
	virtual void ServerReleaseStation(AActor* Station) override;
	virtual void ServerApplyControl(AActor* Station, const FOceanAdventureNavalTargetData& Data) override;
	virtual bool BuildControlSample(FOceanAdventureNavalTargetData& OutData) const override;
	virtual FGameplayTag GetStationStatusTag() const override;
	virtual bool IsStationStillValid(AActor* Station) const override;

private:
	static UNavalHelmComponent* ResolveHelm(AActor* Station);
};
