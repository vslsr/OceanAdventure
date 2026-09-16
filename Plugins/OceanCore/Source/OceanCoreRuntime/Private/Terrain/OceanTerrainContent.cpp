// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/OceanTerrainContent.h"

#include "OceanTerrainHash.h"
#include "Terrain/OceanTerrainBiome.h"

namespace OceanTerrain
{
	namespace
	{
		constexpr uint32 TerrainNoiseSalt = 0x74c319adu;
		constexpr uint32 TerrainSlopeSalt = 0x2b916e47u;

		/** 2^5 cells of feature size, about 64 m. */
		constexpr int32 TerrainNoiseShift = 5;

		/** The spawn ring and its surroundings always keep a flat 22 x 22 m of land. */
		constexpr int32 SpawnSafeRadiusCells = 5;

		struct FCardinalNeighbour
		{
			int32 DeltaX;
			int32 DeltaY;
			EOceanTerrainShape Shape;
		};

		/** Scan order is load-bearing: the rotation below indexes into it. */
		constexpr FCardinalNeighbour CardinalNeighbours[4] = {
			{ 0, 1, EOceanTerrainShape::RampNorth },
			{ 1, 0, EOceanTerrainShape::RampEast },
			{ 0, -1, EOceanTerrainShape::RampSouth },
			{ -1, 0, EOceanTerrainShape::RampWest },
		};

		constexpr EOceanTerrainShape LowCornerShapes[4] = {
			EOceanTerrainShape::CornerLowSouthWest,
			EOceanTerrainShape::CornerLowNorthWest,
			EOceanTerrainShape::CornerLowNorthEast,
			EOceanTerrainShape::CornerLowSouthEast,
		};

		constexpr FCardinalNeighbour DiagonalNeighbours[4] = {
			{ 1, 1, EOceanTerrainShape::CornerHighNorthEast },
			{ 1, -1, EOceanTerrainShape::CornerHighSouthEast },
			{ -1, -1, EOceanTerrainShape::CornerHighSouthWest },
			{ -1, 1, EOceanTerrainShape::CornerHighNorthWest },
		};
	}

	int32 BaseLevelAt(uint32 WorldSeed, int32 CellX, int32 CellY)
	{
		if (FMath::Abs(CellX) <= SpawnSafeRadiusCells && FMath::Abs(CellY) <= SpawnSafeRadiusCells)
		{
			return 0;
		}

		const int32 Noise = ValueNoise(WorldSeed ^ TerrainNoiseSalt, CellX, CellY, TerrainNoiseShift);
		if (Noise < 28)
		{
			return -2;
		}
		if (Noise < 72)
		{
			return -1;
		}
		if (Noise < 166)
		{
			return 0;
		}
		if (Noise < 220)
		{
			return 1;
		}
		return 2;
	}

	int32 CellCodeAt(uint32 WorldSeed, int32 CellX, int32 CellY)
	{
		const int32 HeightLevel = BaseLevelAt(WorldSeed, CellX, CellY);

		// Biome and height are independent: a sea bed carries the ground cover of the region
		// it sits in, so draining it exposes the same land that was there before.
		const EOceanTerrainBiome Biome = BiomeAt(WorldSeed, CellX, CellY);
		if (HeightLevel < 0)
		{
			return EncodeCell(HeightLevel, EOceanTerrainSurface::Water, EOceanTerrainShape::Flat, Biome);
		}

		const int32 First = static_cast<int32>(
			Hash32(WorldSeed, CellX, CellY, static_cast<int32>(TerrainSlopeSalt)) & 3u);

		int32 HigherCardinalMask = 0;
		for (int32 Direction = 0; Direction < 4; ++Direction)
		{
			const FCardinalNeighbour& Neighbour = CardinalNeighbours[Direction];
			// Exactly one level higher, not merely higher: a two-level drop stays a cliff.
			if (BaseLevelAt(WorldSeed, CellX + Neighbour.DeltaX, CellY + Neighbour.DeltaY)
				== HeightLevel + 1)
			{
				HigherCardinalMask |= 1 << Direction;
			}
		}

		// Two adjacent high edges merge into one inner corner, so the same spot does not
		// randomly degenerate into a straight ramp.
		for (int32 Offset = 0; Offset < 4; ++Offset)
		{
			const int32 Direction = (First + Offset) & 3;
			const int32 NextDirection = (Direction + 1) & 3;
			if ((HigherCardinalMask & (1 << Direction)) != 0
				&& (HigherCardinalMask & (1 << NextDirection)) != 0)
			{
				return EncodeCell(
					HeightLevel, EOceanTerrainSurface::Ground, LowCornerShapes[Direction], Biome);
			}
		}

		for (int32 Offset = 0; Offset < 4; ++Offset)
		{
			const int32 Direction = (First + Offset) & 3;
			if ((HigherCardinalMask & (1 << Direction)) != 0)
			{
				return EncodeCell(
					HeightLevel,
					EOceanTerrainSurface::Ground,
					CardinalNeighbours[Direction].Shape,
					Biome);
			}
		}

		// No high edge and only a diagonal plateau: a single high corner fills the junction
		// of the four cells.
		for (int32 Offset = 0; Offset < 4; ++Offset)
		{
			const FCardinalNeighbour& Diagonal = DiagonalNeighbours[(First + Offset) & 3];
			if (BaseLevelAt(WorldSeed, CellX + Diagonal.DeltaX, CellY + Diagonal.DeltaY)
				== HeightLevel + 1)
			{
				return EncodeCell(HeightLevel, EOceanTerrainSurface::Ground, Diagonal.Shape, Biome);
			}
		}

		return EncodeCell(HeightLevel, EOceanTerrainSurface::Ground, EOceanTerrainShape::Flat, Biome);
	}

	double CellCornerHeight(int32 Code, int32 CornerX, int32 CornerY)
	{
		const double Base = CellHeightLevel(Code) * static_cast<double>(HeightStep);
		const double Step = HeightStep;
		switch (CellShape(Code))
		{
		case EOceanTerrainShape::RampNorth:
			return Base + CornerY * Step;
		case EOceanTerrainShape::RampEast:
			return Base + CornerX * Step;
		case EOceanTerrainShape::RampSouth:
			return Base + (1 - CornerY) * Step;
		case EOceanTerrainShape::RampWest:
			return Base + (1 - CornerX) * Step;
		case EOceanTerrainShape::CornerHighNorthEast:
			return Base + FMath::Min(CornerX, CornerY) * Step;
		case EOceanTerrainShape::CornerHighSouthEast:
			return Base + FMath::Min(CornerX, 1 - CornerY) * Step;
		case EOceanTerrainShape::CornerHighSouthWest:
			return Base + FMath::Min(1 - CornerX, 1 - CornerY) * Step;
		case EOceanTerrainShape::CornerHighNorthWest:
			return Base + FMath::Min(1 - CornerX, CornerY) * Step;
		case EOceanTerrainShape::CornerLowNorthEast:
			return Base + (1 - FMath::Min(CornerX, CornerY)) * Step;
		case EOceanTerrainShape::CornerLowSouthEast:
			return Base + (1 - FMath::Min(CornerX, 1 - CornerY)) * Step;
		case EOceanTerrainShape::CornerLowSouthWest:
			return Base + (1 - FMath::Min(1 - CornerX, 1 - CornerY)) * Step;
		case EOceanTerrainShape::CornerLowNorthWest:
			return Base + (1 - FMath::Min(1 - CornerX, CornerY)) * Step;
		default:
			return Base;
		}
	}

	double CellCornerHeight(int32 Code, ECellCorner Corner)
	{
		switch (Corner)
		{
		case ECellCorner::SouthWest:
			return CellCornerHeight(Code, 0, 0);
		case ECellCorner::SouthEast:
			return CellCornerHeight(Code, 1, 0);
		case ECellCorner::NorthEast:
			return CellCornerHeight(Code, 1, 1);
		default:
			return CellCornerHeight(Code, 0, 1);
		}
	}

	FTerrainSample SampleTerrain(
		uint32 WorldSeed,
		double WorldX,
		double WorldY,
		const TFunctionRef<int32(int32, int32)>* CellCodeOverride)
	{
		FTerrainSample Sample;
		Sample.WorldX = WorldX;
		Sample.WorldY = WorldY;
		Sample.CellX = WorldToCell(WorldX);
		Sample.CellY = WorldToCell(WorldY);

		const double LocalX = WorldX / CellSize - Sample.CellX;
		const double LocalY = WorldY / CellSize - Sample.CellY;

		Sample.Code = CellCodeOverride != nullptr
			? (*CellCodeOverride)(Sample.CellX, Sample.CellY)
			: CellCodeAt(WorldSeed, Sample.CellX, Sample.CellY);

		const EOceanTerrainShape Shape = CellShape(Sample.Code);
		const double Base = CellHeightLevel(Sample.Code) * static_cast<double>(HeightStep);

		double Ramp = 0.0;
		double NormalX = 0.0;
		double NormalY = 0.0;
		constexpr double Slope = static_cast<double>(HeightStep) / static_cast<double>(CellSize);

		if (Shape == EOceanTerrainShape::RampNorth)
		{
			Ramp = LocalY;
			NormalY = -Slope;
		}
		else if (Shape == EOceanTerrainShape::RampEast)
		{
			Ramp = LocalX;
			NormalX = -Slope;
		}
		else if (Shape == EOceanTerrainShape::RampSouth)
		{
			Ramp = 1.0 - LocalY;
			NormalY = Slope;
		}
		else if (Shape == EOceanTerrainShape::RampWest)
		{
			Ramp = 1.0 - LocalX;
			NormalX = Slope;
		}
		else if (IsCornerShape(Shape))
		{
			// Fold every corner variant onto the north-east case, remembering the sign of each
			// mirror so the derivative can be folded back afterwards.
			double CornerX = LocalX;
			double CornerY = LocalY;
			double DerivativeX = 1.0;
			double DerivativeY = 1.0;
			if (Shape == EOceanTerrainShape::CornerHighSouthEast
				|| Shape == EOceanTerrainShape::CornerLowSouthEast)
			{
				CornerY = 1.0 - LocalY;
				DerivativeY = -1.0;
			}
			else if (Shape == EOceanTerrainShape::CornerHighSouthWest
				|| Shape == EOceanTerrainShape::CornerLowSouthWest)
			{
				CornerX = 1.0 - LocalX;
				CornerY = 1.0 - LocalY;
				DerivativeX = -1.0;
				DerivativeY = -1.0;
			}
			else if (Shape == EOceanTerrainShape::CornerHighNorthWest
				|| Shape == EOceanTerrainShape::CornerLowNorthWest)
			{
				CornerX = 1.0 - LocalX;
				DerivativeX = -1.0;
			}

			const bool bFollowsX = CornerX <= CornerY;
			const bool bLowCorner = Shape >= EOceanTerrainShape::CornerLowNorthEast;
			Ramp = FMath::Min(CornerX, CornerY);
			if (bLowCorner)
			{
				Ramp = 1.0 - Ramp;
			}
			const double DerivativeSign = bLowCorner ? -1.0 : 1.0;
			if (bFollowsX)
			{
				NormalX = -Slope * DerivativeX * DerivativeSign;
			}
			else
			{
				NormalY = -Slope * DerivativeY * DerivativeSign;
			}
		}

		Sample.Normal = FVector(NormalX, NormalY, 1.0).GetSafeNormal();
		Sample.HeightLevel = CellHeightLevel(Sample.Code);
		Sample.GroundZ = Base + Ramp * HeightStep;
		Sample.Surface = CellSurface(Sample.Code);
		Sample.Biome = CellBiome(Sample.Code);
		Sample.Shape = Shape;
		Sample.bWalkable = Sample.Surface == EOceanTerrainSurface::Ground;
		return Sample;
	}
}
