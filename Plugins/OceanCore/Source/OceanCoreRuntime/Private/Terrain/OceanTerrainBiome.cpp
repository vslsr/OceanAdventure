// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/OceanTerrainBiome.h"

#include "OceanTerrainHash.h"

namespace OceanTerrain
{
	namespace
	{
		/** Climate feature size: 2^5 cells = 64 m, the same scale as the region grid.
		 *
		 * The reference implementation measured this rather than picking it. Larger (128 m)
		 * and a 512 m map holds only four climate lattice points, so a seed often produces a
		 * world with two or three biomes in it; smaller and neighbouring regions stop sharing
		 * lattice points and the map degenerates into mosaic. At the same scale neighbours
		 * correlate, and patches average about 78 m across.
		 */
		constexpr int32 BiomeClimateShift = 5;

		constexpr uint32 BiomeSiteSalt = 0x4d2c8f13u;
		constexpr uint32 BiomeTemperatureSalt = 0x1a7be35du;
		constexpr uint32 BiomeMoistureSalt = 0x63f09c21u;

		/**
		 * Per-site climate jitter, plus or minus 32.
		 *
		 * Without it every region inside one climate band lands on the same biome and the
		 * Voronoi boundary becomes invisible. With it, regions near a threshold split between
		 * two biomes that then interlock along the irregular boundary.
		 */
		constexpr uint32 BiomeVariationMask = 63;
		constexpr int32 BiomeVariationHalf = 32;

		/**
		 * Climate thresholds.
		 *
		 * Value noise is bilinear, so samples bunch up in the middle and thresholds chosen to
		 * "look centred" starve grassland down to under a fifth of the map. These are solved
		 * from quantiles instead: snow takes the coldest 15%, sand the intersection of the
		 * warmest 40% and the driest 37.5%, mud the wettest ~18%, rock the driest third of
		 * what is left in the temperate band, and grassland keeps the remainder.
		 *
		 * Measured over 125 seeds across a 384 m play area: grassland 37%, sand 16%, mud 15%,
		 * snow 17%, rock 15%, with no seed missing a biome entirely.
		 */
		constexpr int32 SnowTemperatureMaximum = 78;
		constexpr int32 SandTemperatureMinimum = 137;
		constexpr int32 SandMoistureMaximum = 112;
		constexpr int32 MudMoistureMinimum = 172;
		constexpr int32 RockMoistureMaximum = 106;

		uint32 BiomeSiteHash(uint32 WorldSeed, int32 RegionX, int32 RegionY)
		{
			return Hash32(WorldSeed, RegionX, RegionY, static_cast<int32>(BiomeSiteSalt));
		}

		int32 BiomeSiteX(int32 RegionX, uint32 SiteHash)
		{
			return (RegionX << BiomeRegionShift) + BiomeSiteMargin
				+ static_cast<int32>(SiteHash % static_cast<uint32>(BiomeSiteSpan));
		}

		int32 BiomeSiteY(int32 RegionY, uint32 SiteHash)
		{
			return (RegionY << BiomeRegionShift) + BiomeSiteMargin
				+ static_cast<int32>((SiteHash >> 8) % static_cast<uint32>(BiomeSiteSpan));
		}

		int32 ClampClimate(int32 Value)
		{
			return Value < 0 ? 0 : (Value > 255 ? 255 : Value);
		}

		/** Site temperature: the low-frequency band plus the site's own jitter, [0, 255]. */
		int32 SiteTemperature(uint32 WorldSeed, int32 SiteX, int32 SiteY, uint32 SiteHash)
		{
			return ClampClimate(
				ValueNoise(WorldSeed ^ BiomeTemperatureSalt, SiteX, SiteY, BiomeClimateShift)
				+ (static_cast<int32>((SiteHash >> 16) & BiomeVariationMask) - BiomeVariationHalf));
		}

		/** Moisture, built the same way from a different slice of the hash so the two are uncorrelated. */
		int32 SiteMoisture(uint32 WorldSeed, int32 SiteX, int32 SiteY, uint32 SiteHash)
		{
			return ClampClimate(
				ValueNoise(WorldSeed ^ BiomeMoistureSalt, SiteX, SiteY, BiomeClimateShift)
				+ (static_cast<int32>((SiteHash >> 24) & BiomeVariationMask) - BiomeVariationHalf));
		}
	}

	FBiomeSite BiomeRegionSite(uint32 WorldSeed, int32 RegionX, int32 RegionY)
	{
		FBiomeSite Site;
		Site.Hash = BiomeSiteHash(WorldSeed, RegionX, RegionY);
		Site.X = BiomeSiteX(RegionX, Site.Hash);
		Site.Y = BiomeSiteY(RegionY, Site.Hash);
		return Site;
	}

	EOceanTerrainBiome BiomeFromClimate(int32 Temperature, int32 Moisture)
	{
		if (Temperature <= SnowTemperatureMaximum)
		{
			return EOceanTerrainBiome::Snow;
		}
		if (Temperature >= SandTemperatureMinimum && Moisture <= SandMoistureMaximum)
		{
			return EOceanTerrainBiome::Sand;
		}
		if (Moisture >= MudMoistureMinimum)
		{
			return EOceanTerrainBiome::Mud;
		}
		if (Moisture <= RockMoistureMaximum)
		{
			return EOceanTerrainBiome::Rock;
		}
		return EOceanTerrainBiome::Grassland;
	}

	EOceanTerrainBiome BiomeAt(uint32 WorldSeed, int32 CellX, int32 CellY)
	{
		const int32 RegionX = CellX >> BiomeRegionShift;
		const int32 RegionY = CellY >> BiomeRegionShift;

		int32 NearestDistance = MAX_int32;
		int32 NearestX = 0;
		int32 NearestY = 0;
		uint32 NearestHash = 0;

		// Ties go to whichever site the scan reaches first. Both ports walk the offsets in
		// this order, so "first" is the same answer on every platform.
		for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
		{
			for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
			{
				const int32 CandidateRegionX = RegionX + OffsetX;
				const int32 CandidateRegionY = RegionY + OffsetY;
				const uint32 SiteHash = BiomeSiteHash(WorldSeed, CandidateRegionX, CandidateRegionY);
				const int32 SiteX = BiomeSiteX(CandidateRegionX, SiteHash);
				const int32 SiteY = BiomeSiteY(CandidateRegionY, SiteHash);
				const int32 DeltaX = SiteX - CellX;
				const int32 DeltaY = SiteY - CellY;
				const int32 Distance = DeltaX * DeltaX + DeltaY * DeltaY;
				if (Distance < NearestDistance)
				{
					NearestDistance = Distance;
					NearestX = SiteX;
					NearestY = SiteY;
					NearestHash = SiteHash;
				}
			}
		}

		return BiomeFromClimate(
			SiteTemperature(WorldSeed, NearestX, NearestY, NearestHash),
			SiteMoisture(WorldSeed, NearestX, NearestY, NearestHash));
	}
}
