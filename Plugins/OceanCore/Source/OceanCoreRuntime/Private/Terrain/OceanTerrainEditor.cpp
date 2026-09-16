// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/OceanTerrainEditor.h"

#include "Terrain/OceanTerrainWater.h"

namespace OceanTerrain
{
	namespace
	{
		constexpr int32 CardinalOffsets[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };

		EOceanTerrainShape RampShapeFor(ECellDirection Direction)
		{
			switch (Direction)
			{
			case ECellDirection::North:
				return EOceanTerrainShape::RampNorth;
			case ECellDirection::East:
				return EOceanTerrainShape::RampEast;
			case ECellDirection::South:
				return EOceanTerrainShape::RampSouth;
			default:
				return EOceanTerrainShape::RampWest;
			}
		}
	}

	FTerrainCellView FTerrainEditor::ReadCell(int32 CellX, int32 CellY) const
	{
		const int32 Code = Patches.CellCodeAt(CellX, CellY);

		auto CodeLookup = [this](int32 X, int32 Y) { return Patches.CellCodeAt(X, Y); };
		const TFunctionRef<int32(int32, int32)> Override(CodeLookup);
		const FTerrainSample Sample = SampleTerrain(
			Patches.GetWorldSeed(),
			(CellX + 0.5) * CellSize,
			(CellY + 0.5) * CellSize,
			&Override);

		FTerrainCellView View;
		View.CellX = CellX;
		View.CellY = CellY;
		View.Code = Code;
		View.HeightLevel = CellHeightLevel(Code);
		View.Surface = CellSurface(Code);
		View.Biome = CellBiome(Code);
		View.Shape = CellShape(Code);
		View.BedZ = Sample.GroundZ;
		View.WaterDepth = OceanTerrain::WaterDepth(Sample, SeaLevel);
		View.bPatched = Patches.HasCell(CellX, CellY);
		return View;
	}

	bool FTerrainEditor::SetCell(int32 CellX, int32 CellY, const FTerrainCellEdit& Edit)
	{
		const int32 Previous = Patches.CellCodeAt(CellX, CellY);
		const int32 PreviousHeight = CellHeightLevel(Previous);
		const EOceanTerrainBiome Biome = CellBiome(Previous);

		const int32 HeightLevel = FMath::Clamp(
			Edit.HeightLevel.Get(PreviousHeight), MinimumHeightLevel, MaximumHeightLevel);
		const EOceanTerrainShape Shape = Edit.Shape.Get(CellShape(Previous));
		checkf(
			static_cast<int32>(Shape) < ShapeCount,
			TEXT("unknown terrain shape %d"),
			static_cast<int32>(Shape));

		const bool bExplicitSurface = Edit.Surface.IsSet();
		EOceanTerrainSurface Surface = Edit.Surface.Get(CellSurface(Previous));

		if (!bExplicitSurface
			&& Surface == EOceanTerrainSurface::Ground
			&& HeightLevel < PreviousHeight)
		{
			const int32 Candidate = EncodeCell(HeightLevel, Surface, Shape, Biome);
			if (CellMinimumBedHeight(Candidate) < SeaLevel && HasAdjacentWater(CellX, CellY))
			{
				Surface = EOceanTerrainSurface::Water;
			}
		}

		if (!bExplicitSurface
			&& Surface == EOceanTerrainSurface::Water
			&& HeightLevel > PreviousHeight
			&& !CellHasWater(EncodeCell(HeightLevel, Surface, Shape, Biome), SeaLevel))
		{
			Surface = EOceanTerrainSurface::Ground;
		}

		return Patches.SetCellCode(CellX, CellY, EncodeCell(HeightLevel, Surface, Shape, Biome));
	}

	bool FTerrainEditor::SetHeightLevel(int32 CellX, int32 CellY, int32 HeightLevel)
	{
		FTerrainCellEdit Edit;
		Edit.HeightLevel = HeightLevel;
		return SetCell(CellX, CellY, Edit);
	}

	bool FTerrainEditor::Raise(int32 CellX, int32 CellY, int32 Steps)
	{
		checkf(Steps >= 0, TEXT("raise steps cannot be negative"));
		const int32 Current = CellHeightLevel(Patches.CellCodeAt(CellX, CellY));
		return SetHeightLevel(CellX, CellY, FMath::Min(MaximumHeightLevel, Current + Steps));
	}

	bool FTerrainEditor::Lower(int32 CellX, int32 CellY, int32 Steps)
	{
		checkf(Steps >= 0, TEXT("lower steps cannot be negative"));
		const int32 Current = CellHeightLevel(Patches.CellCodeAt(CellX, CellY));
		return SetHeightLevel(CellX, CellY, FMath::Max(MinimumHeightLevel, Current - Steps));
	}

	bool FTerrainEditor::SetSurface(int32 CellX, int32 CellY, EOceanTerrainSurface Surface)
	{
		FTerrainCellEdit Edit;
		Edit.Surface = Surface;
		return SetCell(CellX, CellY, Edit);
	}

	bool FTerrainEditor::SetShape(int32 CellX, int32 CellY, EOceanTerrainShape Shape)
	{
		FTerrainCellEdit Edit;
		Edit.Shape = Shape;
		return SetCell(CellX, CellY, Edit);
	}

	bool FTerrainEditor::SetRamp(int32 CellX, int32 CellY, ECellDirection Direction)
	{
		return SetShape(CellX, CellY, RampShapeFor(Direction));
	}

	bool FTerrainEditor::Flatten(int32 CellX, int32 CellY)
	{
		return SetShape(CellX, CellY, EOceanTerrainShape::Flat);
	}

	bool FTerrainEditor::Flood(int32 CellX, int32 CellY)
	{
		const int32 Previous = Patches.CellCodeAt(CellX, CellY);
		const EOceanTerrainShape Shape = CellShape(Previous);
		const EOceanTerrainBiome Biome = CellBiome(Previous);

		int32 HeightLevel = CellHeightLevel(Previous);
		int32 Code = EncodeCell(HeightLevel, EOceanTerrainSurface::Water, Shape, Biome);
		while (!CellHasWater(Code, SeaLevel) && HeightLevel > MinimumHeightLevel)
		{
			--HeightLevel;
			Code = EncodeCell(HeightLevel, EOceanTerrainSurface::Water, Shape, Biome);
		}
		return Patches.SetCellCode(CellX, CellY, Code);
	}

	bool FTerrainEditor::ResetCell(int32 CellX, int32 CellY)
	{
		return Patches.ResetCell(CellX, CellY);
	}

	bool FTerrainEditor::HasAdjacentWater(int32 CellX, int32 CellY) const
	{
		for (const int32(&Offset)[2] : CardinalOffsets)
		{
			if (CellHasWater(Patches.CellCodeAt(CellX + Offset[0], CellY + Offset[1]), SeaLevel))
			{
				return true;
			}
		}
		return false;
	}
}
