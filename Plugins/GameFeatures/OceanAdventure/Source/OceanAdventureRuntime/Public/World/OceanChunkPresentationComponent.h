// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "OceanChunkPresentationComponent.generated.h"

class AOceanChunkActor;
class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Local presentation for one OceanCore chunk.
 *
 * OceanCore owns replicated chunk state. OceanAdventure injects this component and owns
 * the concrete terrain/water assets, keeping the reusable world model free of art content.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Ocean), meta = (BlueprintSpawnableComponent))
class OCEANADVENTURERUNTIME_API UOceanChunkPresentationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOceanChunkPresentationComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UFUNCTION()
	void HandleChunkInitialized(AOceanChunkActor* Chunk);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Terrain")
	TSoftObjectPtr<UMaterialInterface> TerrainMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Water")
	TSoftObjectPtr<UStaticMesh> WaterMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Water")
	TSoftObjectPtr<UMaterialInterface> WaterMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Terrain", meta = (ClampMin = "2", ClampMax = "128"))
	int32 TerrainQuadsPerSide = 48;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Terrain", meta = (ClampMin = "100.0"))
	float TerrainNoiseScale = 12000.0f;

	/** Surface roughness on top of the island shape, in cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Terrain", meta = (ClampMin = "0.0"))
	float TerrainHeightAmplitude = 900.0f;

	/** Seabed height where no island reaches, in cm. Must be well below WaterHeight. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Terrain")
	float OceanFloorHeight = -4000.0f;

	/** Height at an island centre, in cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Island")
	float IslandPeakHeight = 2600.0f;

	/** The world is cut into squares this wide, each holding at most one island centre. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Island", meta = (ClampMin = "1000.0"))
	float IslandCellSize = 60000.0f;

	/** Nominal island footprint radius in cm; each island varies around this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Island", meta = (ClampMin = "100.0"))
	float IslandRadius = 18000.0f;

	/** Fraction of cells that actually carry an island. 1 puts one in every cell. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Island", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IslandDensity = 0.55f;

	/** How much noise warps the coastline. 0 gives perfect circles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Island", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IslandEdgeDistortion = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Water")
	float WaterHeight = 0.0f;

private:
	void BuildPresentation(AOceanChunkActor& Chunk);
	void BuildTerrain(AOceanChunkActor& Chunk);
	void BuildWater(AOceanChunkActor& Chunk);
	void DestroyPresentation();
	float GetTerrainHeight(const AOceanChunkActor& Chunk, double WorldX, double WorldY) const;
	float GetIslandMask(const AOceanChunkActor& Chunk, const FVector2D& WorldPosition) const;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> TerrainMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WaterMeshComponent;

	bool bPresentationBuilt = false;
};
