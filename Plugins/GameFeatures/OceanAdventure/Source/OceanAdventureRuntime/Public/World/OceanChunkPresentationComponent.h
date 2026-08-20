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
	int32 TerrainQuadsPerSide = 32;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Terrain", meta = (ClampMin = "100.0"))
	float TerrainNoiseScale = 12000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Terrain", meta = (ClampMin = "0.0"))
	float TerrainHeightAmplitude = 450.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Terrain")
	float TerrainBaseHeight = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean Adventure|Water")
	float WaterHeight = 0.0f;

private:
	void BuildPresentation(AOceanChunkActor& Chunk);
	void BuildTerrain(AOceanChunkActor& Chunk);
	void BuildWater(AOceanChunkActor& Chunk);
	void DestroyPresentation();
	float GetTerrainHeight(const AOceanChunkActor& Chunk, double WorldX, double WorldY) const;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> TerrainMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WaterMeshComponent;

	bool bPresentationBuilt = false;
};
