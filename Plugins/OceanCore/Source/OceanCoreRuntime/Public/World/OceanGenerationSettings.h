// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "OceanGenerationSettings.generated.h"

/**
 * Shared parameters for deterministic ocean island generation.
 *
 * Sampling depends only on this asset and world position, so adjacent chunks, clients,
 * servers, and editor previews resolve the same coastline without storing generated state.
 */
UCLASS(BlueprintType)
class OCEANCORERUNTIME_API UOceanGenerationSettings : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Returns the normalized 0..1 island mask at a world-space XY position. */
	UFUNCTION(BlueprintPure, Category = "Ocean|Generation")
	float SampleIslandMask(FVector2D WorldPosition) const;

	/** Returns the generated terrain Z at a world-space XY position, in centimeters. */
	UFUNCTION(BlueprintPure, Category = "Ocean|Generation")
	float SampleTerrainHeight(FVector2D WorldPosition) const;

	int32 GetWorldSeed() const { return WorldSeed; }
	float GetChunkSize() const { return FMath::Max(100.0f, ChunkSize); }
	int32 GetTerrainResolution() const { return FMath::Clamp(TerrainResolution, 2, 256); }
	float GetTerrainNoiseScale() const { return FMath::Max(100.0f, TerrainNoiseScale); }
	float GetTerrainHeightAmplitude() const { return FMath::Max(0.0f, TerrainHeightAmplitude); }
	float GetOceanFloorHeight() const { return OceanFloorHeight; }
	float GetIslandPeakHeight() const { return IslandPeakHeight; }
	float GetIslandCellSize() const { return FMath::Max(1000.0f, IslandCellSize); }
	float GetIslandRadius() const { return FMath::Max(100.0f, IslandRadius); }
	float GetUnderwaterShelfWidth() const { return FMath::Max(100.0f, UnderwaterShelfWidth); }
	float GetBeachSlopeAngleDegrees() const
	{
		return FMath::Clamp(BeachSlopeAngleDegrees, 5.0f, 40.0f);
	}
	float GetIslandPlateauRadiusRatio() const
	{
		return FMath::Clamp(IslandPlateauRadiusRatio, 0.0f, 0.95f);
	}
	float GetIslandDensity() const { return FMath::Clamp(IslandDensity, 0.0f, 1.0f); }
	float GetIslandEdgeDistortion() const
	{
		return FMath::Clamp(IslandEdgeDistortion, 0.0f, 1.0f);
	}
	float GetWaterHeight() const { return WaterHeight; }
	float GetBeachBandHeight() const { return FMath::Max(0.0f, BeachBandHeight); }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|World")
	int32 WorldSeed = 12345;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|World",
		meta = (ClampMin = "100.0", UIMin = "1000.0", Units = "cm"))
	float ChunkSize = 20000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Terrain",
		meta = (ClampMin = "2", ClampMax = "256", UIMin = "8", UIMax = "128"))
	int32 TerrainResolution = 48;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Terrain",
		meta = (ClampMin = "100.0", Units = "cm"))
	float TerrainNoiseScale = 12000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Terrain",
		meta = (ClampMin = "0.0", Units = "cm"))
	float TerrainHeightAmplitude = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Terrain", meta = (Units = "cm"))
	float OceanFloorHeight = -4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Island", meta = (Units = "cm"))
	float IslandPeakHeight = 2600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Island",
		meta = (ClampMin = "1000.0", Units = "cm"))
	float IslandCellSize = 60000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Island",
		meta = (ClampMin = "100.0", Units = "cm"))
	float IslandRadius = 18000.0f;

	/** Horizontal distance from the shoreline to the deep ocean floor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Island",
		meta = (ClampMin = "100.0", UIMin = "1000.0", Units = "cm"))
	float UnderwaterShelfWidth = 8000.0f;

	/** Target incline from the waterline to the plateau; smaller islands shorten their plateau to preserve it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Island",
		meta = (ClampMin = "5.0", ClampMax = "40.0", UIMin = "5.0", UIMax = "40.0", Units = "deg"))
	float BeachSlopeAngleDegrees = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Island",
		meta = (ClampMin = "0.0", ClampMax = "0.95", UIMin = "0.0", UIMax = "0.95"))
	float IslandPlateauRadiusRatio = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Island",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float IslandDensity = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Island",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float IslandEdgeDistortion = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Water", meta = (Units = "cm"))
	float WaterHeight = 0.0f;

	/** Height range above the waterline displayed as the yellow island edge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Water",
		meta = (ClampMin = "0.0", Units = "cm"))
	float BeachBandHeight = 250.0f;
};
