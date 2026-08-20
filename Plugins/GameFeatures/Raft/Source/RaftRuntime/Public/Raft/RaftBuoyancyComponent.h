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

	TWeakObjectPtr<UOceanWorldManagerComponent> CachedWorldManager;
	float LastSampledSurfaceHeight = 0.0f;
	bool bAppliedInitialSurface = false;
};
