// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/OceanTerrainWater.h"

namespace OceanTerrain
{
	double CellTopHeight(int32 Code)
	{
		return FMath::Max(
			FMath::Max(CellCornerHeight(Code, 0, 0), CellCornerHeight(Code, 1, 0)),
			FMath::Max(CellCornerHeight(Code, 1, 1), CellCornerHeight(Code, 0, 1)));
	}

	double CellMinimumBedHeight(int32 Code)
	{
		return FMath::Min(
			FMath::Min(CellCornerHeight(Code, 0, 0), CellCornerHeight(Code, 1, 0)),
			FMath::Min(CellCornerHeight(Code, 1, 1), CellCornerHeight(Code, 0, 1)));
	}

	double WaterDepth(const FTerrainSample& Sample, double SeaLevel)
	{
		if (Sample.Surface != EOceanTerrainSurface::Water)
		{
			return 0.0;
		}
		return FMath::Max(0.0, SeaLevel - Sample.GroundZ);
	}

	bool SampleHasWater(const FTerrainSample& Sample, double SeaLevel)
	{
		return WaterDepth(Sample, SeaLevel) > WaterDepthEpsilon;
	}

	double SurfaceHeight(const FTerrainSample& Sample, double SeaLevel)
	{
		return SampleHasWater(Sample, SeaLevel) ? SeaLevel : Sample.GroundZ;
	}

	bool CellHasWater(int32 Code, double SeaLevel)
	{
		if (CellSurface(Code) != EOceanTerrainSurface::Water)
		{
			return false;
		}
		return CellMinimumBedHeight(Code) < SeaLevel - WaterDepthEpsilon;
	}

	double MovementHeight(
		const FTerrainSample& Sample,
		double SeaLevel,
		bool bFloats,
		double BuoyancyDraft)
	{
		if (bFloats && SampleHasWater(Sample, SeaLevel))
		{
			return FMath::Max(Sample.GroundZ, SeaLevel - FMath::Max(0.0, BuoyancyDraft));
		}
		return Sample.GroundZ;
	}
}
