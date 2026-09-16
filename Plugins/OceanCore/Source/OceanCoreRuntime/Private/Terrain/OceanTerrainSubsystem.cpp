// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/OceanTerrainSubsystem.h"

#include "Engine/World.h"
#include "OceanCoreRuntimeModule.h"
#include "Terrain/OceanTerrainChunkComponent.h"

void UOceanTerrainSubsystem::Deinitialize()
{
	if (Store.IsValid() && PatchChangedHandle.IsValid())
	{
		Store->OnPatchChanged().Remove(PatchChangedHandle);
		PatchChangedHandle.Reset();
	}
	Editor.Reset();
	Store.Reset();
	Chunks.Reset();

	Super::Deinitialize();
}

UOceanTerrainSubsystem* UOceanTerrainSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<UOceanTerrainSubsystem>() : nullptr;
}

OceanTerrain::FTerrainPatchStore& UOceanTerrainSubsystem::EnsureStore(uint32 WorldSeed)
{
	if (Store.IsValid())
	{
		if (Store->GetWorldSeed() != WorldSeed)
		{
			UE_LOG(
				LogOceanCore,
				Error,
				TEXT("Terrain store was created for seed %u and is now being asked for seed %u. ")
				TEXT("The %d stored edits are differences from the first world, so they mean ")
				TEXT("something else in the second one. Keeping the original seed."),
				Store->GetWorldSeed(),
				WorldSeed,
				Store->Num());
		}
		return *Store;
	}

	Store = MakeUnique<OceanTerrain::FTerrainPatchStore>(WorldSeed);
	Editor = MakeUnique<OceanTerrain::FTerrainEditor>(*Store, SeaLevel);
	PatchChangedHandle = Store->OnPatchChanged().AddUObject(
		this, &UOceanTerrainSubsystem::HandlePatchChanged);
	return *Store;
}

void UOceanTerrainSubsystem::RegisterChunk(UOceanTerrainChunkComponent* Chunk, FIntPoint ChunkCoord)
{
	if (Chunk)
	{
		Chunks.Add(ChunkCoord, Chunk);
	}
}

void UOceanTerrainSubsystem::UnregisterChunk(UOceanTerrainChunkComponent* Chunk)
{
	for (auto It = Chunks.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid() || It.Value().Get() == Chunk)
		{
			It.RemoveCurrent();
		}
	}
}

void UOceanTerrainSubsystem::CollectWindowOverrides(
	FIntPoint ChunkCoord,
	TArray<int32>& OutTriples) const
{
	OutTriples.Reset();
	if (Store.IsValid())
	{
		Store->CollectWindowOverrides(ChunkCoord, OutTriples);
	}
}

bool UOceanTerrainSubsystem::RaiseCell(int32 CellX, int32 CellY, int32 Steps)
{
	return Editor.IsValid() && Editor->Raise(CellX, CellY, Steps);
}

bool UOceanTerrainSubsystem::LowerCell(int32 CellX, int32 CellY, int32 Steps)
{
	return Editor.IsValid() && Editor->Lower(CellX, CellY, Steps);
}

bool UOceanTerrainSubsystem::FlattenCell(int32 CellX, int32 CellY)
{
	return Editor.IsValid() && Editor->Flatten(CellX, CellY);
}

bool UOceanTerrainSubsystem::ResetCell(int32 CellX, int32 CellY)
{
	return Editor.IsValid() && Editor->ResetCell(CellX, CellY);
}

int32 UOceanTerrainSubsystem::GetEditedCellCount() const
{
	return Store.IsValid() ? Store->Num() : 0;
}

void UOceanTerrainSubsystem::HandlePatchChanged(const OceanTerrain::FTerrainPatchChange& Change)
{
	// AffectedChunksForCell already includes the neighbours whose code window reaches across
	// the border to read this cell, which is why an edit on a chunk's western or southern edge
	// rebuilds two chunks rather than one.
	for (const FIntPoint& ChunkCoord : Change.AffectedChunks)
	{
		if (TWeakObjectPtr<UOceanTerrainChunkComponent>* Found = Chunks.Find(ChunkCoord))
		{
			if (UOceanTerrainChunkComponent* Chunk = Found->Get())
			{
				Chunk->RequestRebuild();
			}
		}
	}
}
