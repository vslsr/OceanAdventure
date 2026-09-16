// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Terrain/OceanTerrainTypes.h"

/**
 * Sparse edit overlay on top of the procedural terrain.
 *
 * Ported from SkyLand shared/world/terrainPatches.mjs. Migration plan §4.5.
 *
 * The default world stays a pure function of (WorldSeed, CellX, CellY); only cells that
 * differ from it are stored, bucketed by chunk. Memory therefore tracks the number of cells
 * players actually edited, not the area of the world, and editing one cell is O(1).
 *
 * Two behaviours here are load-bearing rather than incidental:
 *
 *   - Writing a cell back to its procedural value DELETES the entry instead of storing an
 *     identity override. Without that the store grows forever as players undo their own work,
 *     and every save and every join payload grows with it.
 *   - Changing a cell notifies the chunks that actually read it, which is not just the chunk
 *     the cell sits in. A chunk's mesh samples one row and column past its own edge to build
 *     cliffs, so an edit on a chunk's western or southern border also changes the neighbour
 *     there. Miss that and the symptom is a stale strip of terrain along a chunk seam.
 */
namespace OceanTerrain
{
	/** What changed, and which chunks have to rebuild because of it. */
	struct FTerrainPatchChange
	{
		int32 CellX = 0;
		int32 CellY = 0;
		TArray<FIntPoint, TInlineAllocator<3>> AffectedChunks;
	};

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnTerrainPatchChanged, const FTerrainPatchChange&);

	/**
	 * Chunks whose geometry depends on one cell.
	 *
	 * Always the chunk containing the cell; plus the western neighbour when the cell sits on
	 * the chunk's local column zero, and the southern neighbour on local row zero, because
	 * those are the chunks whose code window reaches across to read it.
	 *
	 * The south-west diagonal neighbour is deliberately absent. Its window does contain the
	 * cell, at the very corner, but nothing in the builder ever reads that corner -- the east
	 * cliff of the last column and the north cliff of the last row stop one short of it. Adding
	 * it would only cost a rebuild that changes nothing.
	 */
	OCEANCORERUNTIME_API void AffectedChunksForCell(
		int32 CellX,
		int32 CellY,
		TArray<FIntPoint, TInlineAllocator<3>>& OutChunks);

	class OCEANCORERUNTIME_API FTerrainPatchStore
	{
	public:
		explicit FTerrainPatchStore(uint32 InWorldSeed) : WorldSeed(InWorldSeed) {}

		uint32 GetWorldSeed() const { return WorldSeed; }

		/** Number of overridden cells. Unedited regions cost nothing. */
		int32 Num() const { return Size; }

		/** The override if there is one, otherwise the procedural terrain. */
		int32 CellCodeAt(int32 CellX, int32 CellY) const;

		bool HasCell(int32 CellX, int32 CellY) const;

		/**
		 * Stores a code that differs from the procedural terrain. Writing the procedural value
		 * back clears the override instead of storing it.
		 *
		 * @return whether the overlay actually changed.
		 */
		bool SetCellCode(int32 CellX, int32 CellY, int32 Code);

		bool ResetCell(int32 CellX, int32 CellY);

		/** Drops every override. */
		void Reset();

		/**
		 * Every override as a flat [CellX, CellY, Code, ...] list -- the shape BuildChunkCodes
		 * takes, and the shape that crosses the network when a player joins.
		 */
		void Entries(TArray<int32>& OutTriples) const;

		/**
		 * Overrides that can affect one chunk's geometry, as the same flat triples.
		 *
		 * Gathers the four chunks the code window overlaps; BuildChunkCodes discards anything
		 * that falls outside the window, so no filtering is needed here.
		 */
		void CollectWindowOverrides(FIntPoint ChunkCoord, TArray<int32>& OutTriples) const;

		FOnTerrainPatchChanged& OnPatchChanged() { return PatchChanged; }

	private:
		void Broadcast(int32 CellX, int32 CellY) const;

		uint32 WorldSeed;
		int32 Size = 0;

		/** Chunk coordinate -> (local cell index -> code). */
		TMap<FIntPoint, TMap<int32, int32>> Chunks;

		mutable FOnTerrainPatchChanged PatchChanged;
	};
}
