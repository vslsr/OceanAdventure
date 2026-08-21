// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PawnComponent.h"

#include "OceanAdventureBuildProximityComponent.generated.h"

class UBuildStructureComponent;

/**
 * Grants and clears Status.Build.HostAvailable as the player moves in and out of range of a
 * buildable host.
 *
 * A tag rather than granting and revoking the abilities themselves: revoking an ability spec
 * would interrupt whatever is currently running, and the raft's bobbing plus the player walking
 * near the edge keep the distance oscillating around the threshold. Lyra's own
 * UAbilityTask_GrantNearbyInteraction only ever grants, never revokes, for the same reason.
 *
 * The scan asks the world's structure registry rather than running a sphere overlap, so it costs
 * no scene query at all.
 */
UCLASS(BlueprintType, ClassGroup = (OceanAdventure), meta = (BlueprintSpawnableComponent))
class OCEANADVENTURERUNTIME_API UOceanAdventureBuildProximityComponent : public UPawnComponent
{
	GENERATED_BODY()

public:
	UOceanAdventureBuildProximityComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Building")
	UBuildStructureComponent* GetNearestHost() const { return NearestHost.Get(); }

protected:
	/** Distance at which a host starts counting as reachable. */
	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Building", meta = (ClampMin = "100.0", Units = "cm"))
	double EnterRadius = 2000.0;

	/** Hysteresis multiplier for leaving; keeps the tag from flickering at the boundary. */
	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Building", meta = (ClampMin = "1.0"))
	double ExitRadiusScale = 1.25;

	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Building", meta = (ClampMin = "0.05", Units = "s"))
	float ScanInterval = 0.25f;

private:
	void ScanForHost();
	void SetHostAvailable(bool bAvailable);

	TWeakObjectPtr<UBuildStructureComponent> NearestHost;
	FTimerHandle ScanTimerHandle;
	bool bHostAvailable = false;
};
