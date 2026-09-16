// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DynamicMeshComponent.h"
#include "CoreMinimal.h"
#include "Terrain/OceanTerrainMeshBuilder.h"
#include "Tasks/Task.h"

#include "OceanTerrainChunkComponent.generated.h"

class AOceanChunkActor;

/**
 * Draws and collides one chunk of stepped terrain.
 *
 * Migration plan §4.7 and §4.8. The component derives from UDynamicMeshComponent so the fill
 * mesh and the collision body are the same geometry by construction rather than by
 * convention -- there is no second, simplified mesh for either side to drift towards.
 *
 * Collision is a triangle mesh, not a height field. Every step in this terrain has a vertical
 * face, and a height field cannot express one; the reference implementation reached the same
 * conclusion and says so in terrainCollisionMesh.mjs.
 *
 * SELF-WIRING. The component finds the AOceanChunkActor that owns it and builds when that
 * chunk initialises, so it can be attached from a Blueprint or a GameFeature action without
 * AOceanChunkActor knowing terrain exists. That keeps chunk streaming and terrain independent,
 * which is what lets the ocean-only maps keep working while terrain lands.
 *
 * INK is produced but not yet drawn. Terrain does not use the inverted hull -- see
 * FTerrainInkData for why -- and the production path is a ribbon strip whose width the
 * material holds constant in screen space (line-art doc §2.1 and §6). That material does not
 * exist yet, so the segments are stored and optionally drawn as debug lines; building a
 * world-width ribbon now would bake in exactly the thing §2.1 warns against.
 */
UCLASS(ClassGroup = (Ocean), meta = (BlueprintSpawnableComponent))
class OCEANCORERUNTIME_API UOceanTerrainChunkComponent : public UDynamicMeshComponent
{
	GENERATED_BODY()

public:
	UOceanTerrainChunkComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Rebuilds this chunk from a seed and a flat [CellX, CellY, Code, ...] override list.
	 *
	 * Safe to call again while a build is in flight; the older result is discarded when it
	 * lands rather than cancelled, because a chunk build is short and a cancellation path is
	 * one more thing that can leave the component half-applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ocean|Terrain")
	void RebuildTerrain(int32 InWorldSeed, FIntPoint InChunkCoord, const TArray<int32>& InOverrides);

	/** Ink segments for this chunk, in component space. Empty until the first build lands. */
	const OceanTerrain::FTerrainInkData& GetInkData() const { return InkData; }

	UFUNCTION(BlueprintPure, Category = "Ocean|Terrain")
	int32 GetInkSegmentCount() const { return InkData.Segments.Num(); }

	UFUNCTION(BlueprintPure, Category = "Ocean|Terrain")
	bool IsTerrainBuilt() const { return bTerrainBuilt; }

protected:
	/** Sea level in component space, used only to shade submerged beds down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Terrain", meta = (Units = "cm"))
	float SeaLevel = 0.0f;

	/** Scene paper tone the five biome tints are mixed from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Terrain")
	FLinearColor GroundColor = FLinearColor(0.82f, 0.78f, 0.70f);

	/** Line-art fill material. Reads the per-vertex biome tint this component writes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Terrain")
	TObjectPtr<UMaterialInterface> FillMaterial;

	/** Draws the ink segments as debug lines until the ribbon material exists. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Terrain|Debug")
	bool bDrawInk = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Terrain|Debug")
	FLinearColor DebugFoldColor = FLinearColor(0.09f, 0.09f, 0.08f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Terrain|Debug")
	FLinearColor DebugCliffColor = FLinearColor(0.75f, 0.20f, 0.10f);

private:
	void BindToOwningChunk();

	UFUNCTION()
	void HandleChunkInitialized(AOceanChunkActor* Chunk);

	/** Applies a finished build on the game thread. */
	void ApplyBuild(OceanTerrain::FTerrainMeshData&& Mesh, OceanTerrain::FTerrainInkData&& Ink);

	void DrawInk() const;

	OceanTerrain::FTerrainInkData InkData;

	UE::Tasks::FTask BuildTask;

	/** Bumped per request so a stale result can recognise itself and bow out. */
	int32 BuildSerial = 0;

	bool bTerrainBuilt = false;
};
