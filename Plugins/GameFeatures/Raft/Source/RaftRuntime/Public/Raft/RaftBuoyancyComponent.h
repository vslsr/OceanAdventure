// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "RaftBuoyancyComponent.generated.h"

class UOceanGenerationSettings;
class UOceanWorldManagerComponent;
class URaftDefinition;

/** Server-only kinematic buoyancy; the owning raft replicates the resulting movement. */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Raft), meta = (BlueprintSpawnableComponent))
class RAFTRUNTIME_API URaftBuoyancyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URaftBuoyancyComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	void ApplyDefinition(const URaftDefinition* Definition);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Raft|Buoyancy")
	void RebuildFromStructure(const FBox& LocalStructureBounds);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Raft|Buoyancy")
	void SetBuoyancyEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Raft|Buoyancy")
	bool IsBuoyancyEnabled() const { return bBuoyancyEnabled; }

	UFUNCTION(BlueprintPure, Category = "Raft|Buoyancy")
	float GetLastSampledSurfaceHeight() const { return LastSampledSurfaceHeight; }

protected:
	/** Optional per-instance override. Null uses the active OceanWorldManager settings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy")
	TObjectPtr<UOceanGenerationSettings> GenerationSettingsOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy")
	TArray<FVector> PontoonOffsets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy", meta = (Units = "cm"))
	float WaterlineOffset = 0.0f;

	/**
	 * Simulation rate falls off with distance to the nearest viewer. Every raft samples the
	 * ocean four times per tick on the server, so a busy world pays for rafts nobody is
	 * looking at. Interpolation stays frame-rate independent because a throttled tick is
	 * handed the accumulated DeltaTime.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy|LOD", meta = (ClampMin = "0.0", Units = "cm"))
	double FullRateDistance = 8000.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy|LOD", meta = (ClampMin = "0.0", Units = "cm"))
	double ReducedRateDistance = 25000.0;

	/** Tick interval used between FullRateDistance and ReducedRateDistance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy|LOD", meta = (ClampMin = "0.0", Units = "s"))
	float ReducedTickInterval = 0.1f;

	/** Tick interval used beyond ReducedRateDistance. Rafts keep drifting, just coarsely. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy|LOD", meta = (ClampMin = "0.0", Units = "s"))
	float DistantTickInterval = 0.5f;

	/** How often the distance itself is re-evaluated. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy|LOD", meta = (ClampMin = "0.1", Units = "s"))
	double DistanceEvaluationInterval = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy",
		meta = (ClampMin = "0.0", ClampMax = "20.0", Units = "deg"))
	float MaxTiltDegrees = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy", meta = (ClampMin = "0.0"))
	float VerticalInterpSpeed = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy", meta = (ClampMin = "0.0"))
	float RotationInterpSpeed = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy")
	bool bSnapToSurfaceOnFirstUpdate = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy")
	bool bBuoyancyEnabled = true;

private:
	bool SampleWaterSurface(const FVector& WorldLocation, float TimeSeconds, float& OutHeight, FVector& OutNormal);
	const UOceanGenerationSettings* ResolveGenerationSettings() const;
	void UpdateSimulationRate(double NowSeconds);

	TWeakObjectPtr<UOceanWorldManagerComponent> CachedWorldManager;
	float LastSampledSurfaceHeight = 0.0f;
	bool bAppliedInitialSurface = false;
	double NextDistanceEvaluationSeconds = 0.0;
};
