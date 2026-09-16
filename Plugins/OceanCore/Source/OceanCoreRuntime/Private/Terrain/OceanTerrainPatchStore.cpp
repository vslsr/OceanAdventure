// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/OceanTerrainPatchStore.h"

#include "Terrain/OceanTerrainContent.h"

namespace OceanTerrain
{
	namespace
	{
		struct FCellAddress
		{
			FIntPoint ChunkCoord;
			int32 LocalX;
			int32 LocalY;
			int32 LocalIndex;
		};

		FCellAddress AddressOf(int32 CellX, int32 CellY)
		{
			const int32 ChunkX = FMath::DivideAndRoundDown(CellX, ChunkGrid);
			const int32 ChunkY = FMath::DivideAndRoundDown(CellY, ChunkGrid);
			const int32 LocalX = CellX - ChunkX * ChunkGrid;
			const int32 LocalY = CellY - ChunkY * ChunkGrid;
			return FCellAddress{
				FIntPoint(ChunkX, ChunkY), LocalX, LocalY, LocalY * ChunkGrid + LocalX };
		}

		/** Strips stray bits and rejects an unknown shape, so only well-formed codes are stored. */
		int32 NormalizeCellCode(int32 Code)
		{
			const EOceanTerrainShape Shape = CellShape(Code);
			checkf(
				static_cast<int32>(Shape) < ShapeCount,
				TEXT("unknown terrain shape %d"),
				static_cast<int32>(Shape));
			return EncodeCell(CellHeightLevel(Code), CellSurface(Code), Shape, CellBiome(Code));
		}
	}

	void AffectedChunksForCell(
		int32 CellX,
		int32 CellY,
		TArray<FIntPoint, TInlineAllocator<3>>& OutChunks)
	{
		const FCellAddress Address = AddressOf(CellX, CellY);
		OutChunks.Reset();
		OutChunks.Add(Address.ChunkCoord);
		if (Address.LocalX == 0)
		{
			OutChunks.Add(FIntPoint(Address.ChunkCoord.X - 1, Address.ChunkCoord.Y));
		}
		if (Address.LocalY == 0)
		{
			OutChunks.Add(FIntPoint(Address.ChunkCoord.X, Address.ChunkCoord.Y - 1));
		}
	}

	int32 FTerrainPatchStore::CellCodeAt(int32 CellX, int32 CellY) const
	{
		const FCellAddress Address = AddressOf(CellX, CellY);
		if (const TMap<int32, int32>* Chunk = Chunks.Find(Address.ChunkCoord))
		{
			if (const int32* Code = Chunk->Find(Address.LocalIndex))
			{
				return *Code;
			}
		}
		return OceanTerrain::CellCodeAt(WorldSeed, CellX, CellY);
	}

	bool FTerrainPatchStore::HasCell(int32 CellX, int32 CellY) const
	{
		const FCellAddress Address = AddressOf(CellX, CellY);
		const TMap<int32, int32>* Chunk = Chunks.Find(Address.ChunkCoord);
		return Chunk != nullptr && Chunk->Contains(Address.LocalIndex);
	}

	bool FTerrainPatchStore::SetCellCode(int32 CellX, int32 CellY, int32 Code)
	{
		const int32 Normalized = NormalizeCellCode(Code);
		const int32 Baseline = OceanTerrain::CellCodeAt(WorldSeed, CellX, CellY);
		if (Normalized == Baseline)
		{
			// Back to the procedural value: drop the override rather than store an identity one,
			// or the overlay grows every time a player undoes their own work.
			return ResetCell(CellX, CellY);
		}

		const FCellAddress Address = AddressOf(CellX, CellY);
		TMap<int32, int32>& Chunk = Chunks.FindOrAdd(Address.ChunkCoord);
		if (const int32* Existing = Chunk.Find(Address.LocalIndex))
		{
			if (*Existing == Normalized)
			{
				return false;
			}
		}
		else
		{
			++Size;
		}

		Chunk.Add(Address.LocalIndex, Normalized);
		Broadcast(CellX, CellY);
		return true;
	}

	bool FTerrainPatchStore::ResetCell(int32 CellX, int32 CellY)
	{
		const FCellAddress Address = AddressOf(CellX, CellY);
		TMap<int32, int32>* Chunk = Chunks.Find(Address.ChunkCoord);
		if (!Chunk || Chunk->Remove(Address.LocalIndex) == 0)
		{
			return false;
		}

		--Size;
		if (Chunk->Num() == 0)
		{
			Chunks.Remove(Address.ChunkCoord);
		}
		Broadcast(CellX, CellY);
		return true;
	}

	void FTerrainPatchStore::Reset()
	{
		Chunks.Reset();
		Size = 0;
	}

	void FTerrainPatchStore::Entries(TArray<int32>& OutTriples) const
	{
		OutTriples.Reset(Size * 3);
		for (const TPair<FIntPoint, TMap<int32, int32>>& Bucket : Chunks)
		{
			const int32 OriginCellX = Bucket.Key.X * ChunkGrid;
			const int32 OriginCellY = Bucket.Key.Y * ChunkGrid;
			for (const TPair<int32, int32>& Cell : Bucket.Value)
			{
				OutTriples.Add(OriginCellX + (Cell.Key % ChunkGrid));
				OutTriples.Add(OriginCellY + (Cell.Key / ChunkGrid));
				OutTriples.Add(Cell.Value);
			}
		}
	}

	void FTerrainPatchStore::CollectWindowOverrides(
		FIntPoint ChunkCoord,
		TArray<int32>& OutTriples) const
	{
		OutTriples.Reset();
		// The window runs to ChunkGrid inclusive, so it reaches into the eastern, northern, and
		// north-eastern neighbours by one row or column.
		const FIntPoint Buckets[4] = {
			ChunkCoord,
			FIntPoint(ChunkCoord.X + 1, ChunkCoord.Y),
			FIntPoint(ChunkCoord.X, ChunkCoord.Y + 1),
			FIntPoint(ChunkCoord.X + 1, ChunkCoord.Y + 1),
		};

		for (const FIntPoint& Bucket : Buckets)
		{
			const TMap<int32, int32>* Cells = Chunks.Find(Bucket);
			if (!Cells)
			{
				continue;
			}
			const int32 OriginCellX = Bucket.X * ChunkGrid;
			const int32 OriginCellY = Bucket.Y * ChunkGrid;
			for (const TPair<int32, int32>& Cell : *Cells)
			{
				OutTriples.Add(OriginCellX + (Cell.Key % ChunkGrid));
				OutTriples.Add(OriginCellY + (Cell.Key / ChunkGrid));
				OutTriples.Add(Cell.Value);
			}
		}
	}

	void FTerrainPatchStore::Broadcast(int32 CellX, int32 CellY) const
	{
		if (!PatchChanged.IsBound())
		{
			return;
		}
		FTerrainPatchChange Change;
		Change.CellX = CellX;
		Change.CellY = CellY;
		AffectedChunksForCell(CellX, CellY, Change.AffectedChunks);
		PatchChanged.Broadcast(Change);
	}
}
