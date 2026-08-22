// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"

#include "OceanAdventureNavalBeaconActor.generated.h"

class USphereComponent;
class UStaticMeshComponent;

DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnNavalBeaconCaptured, class AOceanAdventureNavalBeaconActor* /*Beacon*/, int32 /*TeamId*/);

/**
 * 骤死信标 -- the thing that stops a P0 match from becoming two players circling each other.
 *
 * It is inert for the first stretch of the match and then switches on, at which point holding
 * it for a continuous window wins outright. Standing in it while an enemy is also inside
 * freezes the capture rather than continuing it, so the beacon forces a fight instead of
 * rewarding whoever tiptoed in first.
 */
UCLASS(BlueprintType, Blueprintable)
class OCEANADVENTURERUNTIME_API AOceanAdventureNavalBeaconActor : public AActor
{
	GENERATED_BODY()

public:
	AOceanAdventureNavalBeaconActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "OceanAdventure|Naval")
	void SetBeaconActive(bool bNewActive);

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	bool IsBeaconActive() const { return bBeaconActive; }

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	float GetCaptureProgress() const { return CaptureProgress; }

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	int32 GetCapturingTeamId() const { return CapturingTeamId; }

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	bool IsContested() const { return bContested; }

	FOnNavalBeaconCaptured OnBeaconCaptured;

protected:
	/** Design 2.3: hold for 30 seconds to win outright. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "1.0", Units = "s"))
	float CaptureSeconds = 30.0f;

	/** Progress lost per second with nobody holding it; slower than gaining, but not free. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "0.0"))
	float DecayPerSecond = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "100.0", Units = "cm"))
	float CaptureRadius = 900.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OceanAdventure|Naval")
	TObjectPtr<USphereComponent> CaptureVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OceanAdventure|Naval")
	TObjectPtr<UStaticMeshComponent> BeaconMesh;

	UPROPERTY(ReplicatedUsing = OnRep_BeaconState)
	bool bBeaconActive = false;

	UPROPERTY(ReplicatedUsing = OnRep_BeaconState)
	float CaptureProgress = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_BeaconState)
	int32 CapturingTeamId = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_BeaconState)
	bool bContested = false;

	UFUNCTION()
	void OnRep_BeaconState();

private:
	/** Which teams currently have a living character inside the ring. */
	void CollectOccupyingTeams(TArray<int32>& OutTeams) const;
	void BroadcastBeaconState() const;
};
