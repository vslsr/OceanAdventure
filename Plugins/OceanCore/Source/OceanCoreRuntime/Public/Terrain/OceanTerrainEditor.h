// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Terrain/OceanTerrainContent.h"
#include "Terrain/OceanTerrainPatchStore.h"
#include "Terrain/OceanTerrainTypes.h"

/**
 * Editing facade over the patch store.
 *
 * Ported from SkyLand shared/world/terrainEditing.mjs. Every operation validates, composes,
 * and ends in one FTerrainPatchStore::SetCellCode, so editing stays O(1) and the change
 * notification stays in one place.
 *
 * Biome never takes part in an edit. Digging a cell out and piling it back up is the same
 * ground: raise a patch of snow and it is still snow.
 */
namespace OceanTerrain
{
	/** What one cell looks like to an editing tool. */
	struct FTerrainCellView
	{
		int32 CellX = 0;
		int32 CellY = 0;
		int32 Code = 0;
		int32 HeightLevel = 0;
		EOceanTerrainSurface Surface = EOceanTerrainSurface::Ground;
		EOceanTerrainBiome Biome = EOceanTerrainBiome::Grassland;
		EOceanTerrainShape Shape = EOceanTerrainShape::Flat;

		/** Bed height at the cell centre, centimetres -- the floor, water or not. */
		double BedZ = 0.0;
		double WaterDepth = 0.0;

		/** Whether an override is stored, as opposed to this being the procedural value. */
		bool bPatched = false;
	};

	/** Fields left unset keep their current value. */
	struct FTerrainCellEdit
	{
		TOptional<int32> HeightLevel;
		TOptional<EOceanTerrainSurface> Surface;
		TOptional<EOceanTerrainShape> Shape;
	};

	class OCEANCORERUNTIME_API FTerrainEditor
	{
	public:
		FTerrainEditor(FTerrainPatchStore& InPatches, double InSeaLevel = 0.0)
			: Patches(InPatches)
			, SeaLevel(InSeaLevel)
		{
		}

		FTerrainCellView ReadCell(int32 CellX, int32 CellY) const;

		/**
		 * Changes one cell atomically.
		 *
		 * Two water rules ride along when the caller does not name a surface explicitly, and
		 * both exist because of a bug that is otherwise invisible until someone plays it:
		 *
		 *   - Lowering ordinary ground below sea level next to real water lets the water in.
		 *     An isolated pit far from any water stays dry, which is what keeps "dig a hole"
		 *     from filling itself.
		 *   - Raising a water cell until the whole cell stands clear of the waterline turns it
		 *     back into ground. Water marks a cell that currently carries a surface, not an
		 *     irreversible origin label; leave it set and the movement code keeps applying
		 *     buoyancy on top of a plateau, pinning the player at water height and calling them
		 *     grounded there.
		 */
		bool SetCell(int32 CellX, int32 CellY, const FTerrainCellEdit& Edit);

		bool SetHeightLevel(int32 CellX, int32 CellY, int32 HeightLevel);
		bool Raise(int32 CellX, int32 CellY, int32 Steps = 1);
		bool Lower(int32 CellX, int32 CellY, int32 Steps = 1);
		bool SetSurface(int32 CellX, int32 CellY, EOceanTerrainSurface Surface);
		bool SetShape(int32 CellX, int32 CellY, EOceanTerrainShape Shape);

		/** Ramp whose high edge faces the given direction. */
		bool SetRamp(int32 CellX, int32 CellY, ECellDirection Direction);

		bool Flatten(int32 CellX, int32 CellY);

		/**
		 * Floods a cell down to the first level that actually holds water.
		 *
		 * Flipping the water flag alone on ground standing above sea level gives a darkened dry
		 * bed with no water surface at all, so this drops the cell until there is real depth.
		 */
		bool Flood(int32 CellX, int32 CellY);

		/** Clears the override, returning the cell to the procedural terrain. */
		bool ResetCell(int32 CellX, int32 CellY);

		double GetSeaLevel() const { return SeaLevel; }

	private:
		bool HasAdjacentWater(int32 CellX, int32 CellY) const;

		FTerrainPatchStore& Patches;
		double SeaLevel;
	};
}
