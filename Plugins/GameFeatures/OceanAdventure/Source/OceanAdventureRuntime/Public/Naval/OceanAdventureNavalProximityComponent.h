// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PawnComponent.h"
#include "Engine/TimerHandle.h"

#include "OceanAdventureNavalProximityComponent.generated.h"

class AActor;

/**
 * Tells the local player when a gun or a wheel is close enough to take.
 *
 * The gap this fills is that walking up to a station currently produces no feedback at all:
 * the only way to learn you are in range is to press the key and either enter the station or
 * collect a Fail.TooFar. So this keeps Status.Naval.StationAvailable and a prompt message up
 * to date as the player moves.
 *
 * What it deliberately does *not* do is grant anything. Lyra's own interaction system
 * (UAbilityTask_GrantNearbyInteraction) hands abilities out on approach, which is right for
 * pickups and doors -- an open set of objects whose interaction logic the player cannot know
 * in advance. The naval stations are the opposite: two kinds, both known when PawnData is
 * authored, both already granted at spawn through DA_AbilitySet_OceanNaval. Granting them
 * again on approach would buy nothing and cost a GiveAbility churn, and Lyra's task never
 * revokes, so it could not take them back on the way out either.
 *
 * Nor is the tag a gate. The station's own CanOperateStation stays the authority; this only
 * decides what to show, and runs on the owning client alone.
 */
UCLASS(BlueprintType, ClassGroup = (OceanAdventure), meta = (BlueprintSpawnableComponent))
class OCEANADVENTURERUNTIME_API UOceanAdventureNavalProximityComponent : public UPawnComponent
{
	GENERATED_BODY()

public:
	UOceanAdventureNavalProximityComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The station the prompt is currently offering, or null. */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	AActor* GetNearestStation() const { return NearestStation.Get(); }

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	bool IsStationAvailable() const { return bStationAvailable; }

protected:
	/**
	 * Fraction of a station's own interaction range at which the prompt appears.
	 *
	 * Note this tightens the *entry* where the build proximity component widens the exit.
	 * That component's tag gates an ability, so its hysteresis has to keep the ability from
	 * being interrupted at the boundary. This tag drives a prompt, and a prompt shown outside
	 * the range the server will accept is simply wrong -- so the slack goes inward, and the
	 * prompt is gone by the time the station would start refusing.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	double EnterRangeScale = 0.9;

	/**
	 * How often to re-check. A quarter second is under the time it takes to walk the last
	 * metre to a gun, so the prompt still appears to follow the player.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "0.05", Units = "s"))
	float ScanInterval = 0.25f;

private:
	void ScanForStation();

	/** True while the player is already at a station, when a "press E" prompt is just noise. */
	bool IsOperatingStation() const;

	void SetNearestStation(AActor* Station);

	TWeakObjectPtr<AActor> NearestStation;
	FTimerHandle ScanTimerHandle;
	bool bStationAvailable = false;
};
