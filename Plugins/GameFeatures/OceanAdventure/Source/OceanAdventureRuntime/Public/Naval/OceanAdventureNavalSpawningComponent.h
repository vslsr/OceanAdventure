// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Player/LyraPlayerSpawningManagerComponent.h"

#include "OceanAdventureNavalSpawningComponent.generated.h"

class AController;

/**
 * Puts a reconnecting player back where they dropped, when there is still a there to go back to.
 *
 * This uses OnFinishRestartPlayer rather than OnChoosePlayerStart on purpose. Returning an
 * actor from OnChoosePlayerStart hands its transform straight to SpawnDefaultPawnFor, and
 * the transform on offer is a station's origin -- the player would be spawned inside the gun,
 * and a blocked spawn point leaves the engine's collision handling to put them somewhere
 * unpredictable, or to fail the spawn outright.
 *
 * OnFinishRestartPlayer runs after the pawn already exists at a normal player start, so the
 * move is attempted from a known-good state and simply declines when there is no room. The
 * worst case is an ordinary spawn, not a stuck or missing pawn.
 */
UCLASS(BlueprintType, ClassGroup = (OceanAdventure), meta = (BlueprintSpawnableComponent))
class OCEANADVENTURERUNTIME_API UOceanAdventureNavalSpawningComponent : public ULyraPlayerSpawningManagerComponent
{
	GENERATED_BODY()

public:
	UOceanAdventureNavalSpawningComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void OnFinishRestartPlayer(AController* Player, const FRotator& StartRotation) override;

	/** How far from the remembered point a clear standing spot may be found. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "0.0", Units = "cm"))
	float ClearanceRadius = 300.0f;
};
