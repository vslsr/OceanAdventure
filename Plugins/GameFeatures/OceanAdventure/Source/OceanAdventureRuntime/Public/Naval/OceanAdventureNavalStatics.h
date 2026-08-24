// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Naval/NavalCoreTypes.h"
#include "Templates/SubclassOf.h"

#include "OceanAdventureNavalStatics.generated.h"

class APlayerController;
class UNavalVesselComponent;

/**
 * One snapshot of everything the P0 HUD has to show, per design 2.4's minimum list:
 * character state, hull / helm core / rudder / propulsion, load and thrust, and the clock
 * on whatever is currently threatening the ship.
 */
USTRUCT(BlueprintType)
struct FOceanAdventureNavalHudState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OceanAdventure|Naval")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "OceanAdventure|Naval")
	ENavalVesselState VesselState = ENavalVesselState::Operational;

	UPROPERTY(BlueprintReadOnly, Category = "OceanAdventure|Naval")
	float HullFraction = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "OceanAdventure|Naval")
	float HelmCoreFraction = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "OceanAdventure|Naval")
	ENavalHelmState HelmState = ENavalHelmState::Normal;

	UPROPERTY(BlueprintReadOnly, Category = "OceanAdventure|Naval")
	float RudderCapability = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "OceanAdventure|Naval")
	float PropulsionCapability = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "OceanAdventure|Naval")
	FNavalLoadState LoadState;

	UPROPERTY(BlueprintReadOnly, Category = "OceanAdventure|Naval")
	float SpeedFraction = 0.0f;

	/** Counts down while foundering; zero otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "OceanAdventure|Naval")
	float FounderSecondsRemaining = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "OceanAdventure|Naval")
	bool bEmergencyRepairAvailable = true;

	UPROPERTY(BlueprintReadOnly, Category = "OceanAdventure|Naval")
	float CaptureProgress = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "OceanAdventure|Naval")
	int32 TeamId = INDEX_NONE;
};

/** Queries shared by the naval abilities and the minimal P0 HUD. */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureNavalStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Nearest station of a class within radius, from the naval registry.
	 *
	 * The registry rather than a sphere overlap: the stations already announce themselves at
	 * BeginPlay, so this costs a walk over a handful of entries and no scene query at all.
	 * Distance is measured to the seat, which is what the station's own accept check uses --
	 * the wheel is not at its vessel's origin.
	 */
	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Naval", meta = (WorldContext = "WorldContextObject"))
	static AActor* FindNearestStationActor(
		const UObject* WorldContextObject,
		const FVector& Origin,
		float Radius,
		TSubclassOf<AActor> StationClass);

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	static UNavalVesselComponent* FindVesselForActor(AActor* Actor);

	/** The vessel the pawn is standing on, if any. */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	static UNavalVesselComponent* FindVesselUnderPawn(APawn* Pawn);

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	static FOceanAdventureNavalHudState GetVesselHudState(AActor* VesselActor);

	/**
	 * Where the top-down cursor is pointing, on the plane the pawn is standing on.
	 *
	 * Falls back to the pawn's facing when the cursor is over open sky, so a heavy weapon
	 * still receives a usable aim point instead of a zero vector.
	 */
	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Naval")
	static bool GetCursorAimLocation(APlayerController* PlayerController, FVector& OutAimLocation);
};
