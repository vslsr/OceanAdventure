// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Terrain/OceanTerrainTypes.h"

/**
 * Deterministic stepped terrain.
 *
 * Ported from SkyLand shared/world/terrainContent.mjs. Terrain is a pure function of
 * (WorldSeed, CellX, CellY): no state is stored, so a client, a dedicated server, and an
 * editor preview derive the same world from the seed alone and nothing about the base
 * terrain has to cross the network. Player edits ride on top as a sparse overlay -- that is
 * the patch layer, migration plan §4.5, not in this file.
 *
 * Coordinates are Unreal's: CellX runs east (+X), CellY runs north (+Y). See the axis
 * mapping note in OceanTerrainTypes.h.
 */
namespace OceanTerrain
{
	/** One point on the terrain surface. Plain struct: this layer stays free of UObject. */
	struct FTerrainSample
	{
		/** World position sampled, centimetres. */
		double WorldX = 0.0;
		double WorldY = 0.0;

		int32 CellX = 0;
		int32 CellY = 0;

		int32 Code = 0;
		int32 HeightLevel = 0;

		/** Surface height at this exact point, centimetres. Ramps and corners interpolate. */
		double GroundZ = 0.0;

		/** Unit surface normal in Unreal axes. */
		FVector Normal = FVector::UpVector;

		EOceanTerrainSurface Surface = EOceanTerrainSurface::Ground;
		EOceanTerrainBiome Biome = EOceanTerrainBiome::Grassland;
		EOceanTerrainShape Shape = EOceanTerrainShape::Flat;

		bool bWalkable = true;
	};

	/**
	 * Terrace level before ramps are fitted. Integer thresholds cut the low-frequency noise
	 * into wide plateaus. Negative levels are sea bed, zero and above ordinary ground.
	 */
	int32 BaseLevelAt(uint32 WorldSeed, int32 CellX, int32 CellY);

	/**
	 * Packed code for one cell.
	 *
	 * A low plateau becomes a ramp where a neighbour sits exactly one level higher: two
	 * adjacent cardinal neighbours higher makes a single low corner, only a diagonal neighbour
	 * higher makes a single high corner. When several directions qualify, a stable hash
	 * rotates the priority so corners do not all lean the same way.
	 */
	int32 CellCodeAt(uint32 WorldSeed, int32 CellX, int32 CellY);

	/** Cell containing a world coordinate. Negative coordinates must floor, not truncate. */
	inline int32 WorldToCell(double WorldValue)
	{
		return static_cast<int32>(FMath::FloorToDouble(WorldValue / CellSize));
	}

	/** Height of one corner, centimetres. CornerX and CornerY are 0 or 1. */
	double CellCornerHeight(int32 Code, int32 CornerX, int32 CornerY);

	/** Height of the named corner. */
	double CellCornerHeight(int32 Code, ECellCorner Corner);

	/**
	 * O(1) surface sample.
	 *
	 * CellCodeOverride lets the caller answer from the patch store instead of the procedural
	 * function; pass nothing for the unedited world.
	 */
	FTerrainSample SampleTerrain(
		uint32 WorldSeed,
		double WorldX,
		double WorldY,
		const TFunctionRef<int32(int32 CellX, int32 CellY)>* CellCodeOverride = nullptr);
}
