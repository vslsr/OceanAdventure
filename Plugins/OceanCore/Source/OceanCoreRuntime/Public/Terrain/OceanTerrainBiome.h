// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Terrain/OceanTerrainTypes.h"

/**
 * Deterministic biome regions.
 *
 * Ported from SkyLand shared/world/terrainBiome.mjs. The structure is worth keeping in mind
 * before touching any constant in here, because it is not "sample one noise texture per
 * cell":
 *
 *   1. the world is cut into regions of BiomeRegionSize cells, and one hash places a site
 *      inside each region;
 *   2. a cell belongs to the nearest site -- a Voronoi partition, so the boundary is
 *      irregular but a region is one solid patch instead of threshold-speckled confetti;
 *   3. each site picks its biome from two low-frequency climate fields, temperature and
 *      moisture. The climate lattice is the same scale as the region grid, so neighbouring
 *      regions share lattice points: they correlate without matching, which makes same-biome
 *      regions grow into bands while regions near a threshold split along that irregular
 *      Voronoi boundary and interlock.
 *
 * All of it is 32-bit integer arithmetic, for the reason given in OceanTerrainHash.h.
 */
namespace OceanTerrain
{
	/** Region edge in cells. 2^5 cells = 64 m, the size of one biome patch. */
	inline constexpr int32 BiomeRegionShift = 5;
	inline constexpr int32 BiomeRegionSize = 1 << BiomeRegionShift;

	/**
	 * How far a site is inset from its region border, and the jitter span left over.
	 *
	 * The inset is not cosmetic: it is what makes scanning only 3x3 regions an exact Voronoi.
	 * A site in the centre region is at most sqrt(2) * (Size - 1 - Margin) ~= 32.5 cells away,
	 * while a site two regions out is at least Size + Margin = 40 cells away and can never
	 * win. Drop the inset and the scan has to widen to 5x5.
	 */
	inline constexpr int32 BiomeSiteMargin = 8;
	inline constexpr int32 BiomeSiteSpan = BiomeRegionSize - BiomeSiteMargin * 2;

	/** Biome of one cell: nearest site in the surrounding 3x3 regions, typed by its climate. */
	EOceanTerrainBiome BiomeAt(uint32 WorldSeed, int32 CellX, int32 CellY);

	/** Climate lookup, exposed so a test can cover the table without hunting for a seed. */
	EOceanTerrainBiome BiomeFromClimate(int32 Temperature, int32 Moisture);

	/** One region's site position and hash. For tests and debug views. */
	struct FBiomeSite
	{
		int32 X = 0;
		int32 Y = 0;
		uint32 Hash = 0;
	};

	FBiomeSite BiomeRegionSite(uint32 WorldSeed, int32 RegionX, int32 RegionY);
}
