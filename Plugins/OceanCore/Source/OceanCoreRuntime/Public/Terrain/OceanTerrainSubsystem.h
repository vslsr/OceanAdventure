// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Terrain/OceanTerrainEditor.h"
#include "Terrain/OceanTerrainPatchStore.h"

#include "OceanTerrainSubsystem.generated.h"

class UOceanTerrainChunkComponent;

/**
 * Owns one world's terrain edits and routes them to the chunks that have to redraw.
 *
 * Migration plan §4.5 and §4.7. The patch store and the chunk components are deliberately
 * kept apart: a chunk knows how to build itself from a seed plus a list of overrides, and it
 * does not know who edits them. This subsystem is the only thing that knows both, which is
 * what keeps UOceanWorldManagerComponent free of any terrain dependency -- ocean-only maps
 * keep streaming chunks exactly as before.
 *
 * Nothing here is replicated yet. The server-authoritative edit channel is P4; today an edit
 * applies locally, which is correct for the editor, a single-player map, and the automation
 * tests, and wrong for a session with more than one player in it.
 */
UCLASS()
class OCEANCORERUNTIME_API UOceanTerrainSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Ocean|Terrain", meta = (WorldContext = "WorldContextObject"))
	static UOceanTerrainSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * Creates the store on first use, bound to this seed.
	 *
	 * A second, different seed is a bug worth shouting about rather than quietly honouring:
	 * the overrides already stored were computed as differences from the first seed, so
	 * re-basing them onto another world silently turns every edit into a different edit.
	 */
	OceanTerrain::FTerrainPatchStore& EnsureStore(uint32 WorldSeed);

	OceanTerrain::FTerrainPatchStore* GetStore() const { return Store.Get(); }

	/** Editing facade over the store. Null until EnsureStore has run. */
	OceanTerrain::FTerrainEditor* GetEditor() const { return Editor.Get(); }

	void RegisterChunk(UOceanTerrainChunkComponent* Chunk, FIntPoint ChunkCoord);
	void UnregisterChunk(UOceanTerrainChunkComponent* Chunk);

	/** Overrides that can affect one chunk's geometry, as flat [CellX, CellY, Code, ...]. */
	void CollectWindowOverrides(FIntPoint ChunkCoord, TArray<int32>& OutTriples) const;

	UFUNCTION(BlueprintCallable, Category = "Ocean|Terrain")
	bool RaiseCell(int32 CellX, int32 CellY, int32 Steps = 1);

	UFUNCTION(BlueprintCallable, Category = "Ocean|Terrain")
	bool LowerCell(int32 CellX, int32 CellY, int32 Steps = 1);

	UFUNCTION(BlueprintCallable, Category = "Ocean|Terrain")
	bool FlattenCell(int32 CellX, int32 CellY);

	UFUNCTION(BlueprintCallable, Category = "Ocean|Terrain")
	bool ResetCell(int32 CellX, int32 CellY);

	UFUNCTION(BlueprintPure, Category = "Ocean|Terrain")
	int32 GetEditedCellCount() const;

	/** Sea level handed to the editor when the store is created. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean|Terrain", meta = (Units = "cm"))
	float SeaLevel = 0.0f;

private:
	void HandlePatchChanged(const OceanTerrain::FTerrainPatchChange& Change);

	TUniquePtr<OceanTerrain::FTerrainPatchStore> Store;
	TUniquePtr<OceanTerrain::FTerrainEditor> Editor;

	TMap<FIntPoint, TWeakObjectPtr<UOceanTerrainChunkComponent>> Chunks;

	FDelegateHandle PatchChangedHandle;
};
