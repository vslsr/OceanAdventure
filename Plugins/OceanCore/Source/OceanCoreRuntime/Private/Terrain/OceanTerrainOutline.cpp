// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/OceanTerrainOutline.h"

#include "Terrain/OceanTerrainContent.h"

namespace OceanTerrain
{
	namespace
	{
		/** Normals come from a small finite set, so an exact-ish comparison is safe here. */
		constexpr double NormalMatchTolerance = 1.0e-6;

		/** Cell-local corner offsets as (CornerX, CornerY) in units of CellSize. */
		constexpr int32 CornerOffsets[4][2] = {
			{ 0, 0 }, // SouthWest
			{ 1, 0 }, // SouthEast
			{ 1, 1 }, // NorthEast
			{ 0, 1 }, // NorthWest
		};
	}

	ECellDiagonal CellDiagonal(EOceanTerrainShape Shape)
	{
		const bool bNorthWestSouthEast = Shape == EOceanTerrainShape::CornerHighNorthWest
			|| Shape == EOceanTerrainShape::CornerHighSouthEast
			|| Shape == EOceanTerrainShape::CornerLowNorthWest
			|| Shape == EOceanTerrainShape::CornerLowSouthEast;
		return bNorthWestSouthEast ? ECellDiagonal::NorthWestSouthEast : ECellDiagonal::SouthWestNorthEast;
	}

	void CellTopTriangles(int32 Code, FCellTriangle& OutFirst, FCellTriangle& OutSecond)
	{
		if (CellDiagonal(CellShape(Code)) == ECellDiagonal::NorthWestSouthEast)
		{
			OutFirst = { { ECellCorner::SouthWest, ECellCorner::NorthWest, ECellCorner::SouthEast } };
			OutSecond = { { ECellCorner::NorthWest, ECellCorner::NorthEast, ECellCorner::SouthEast } };
			return;
		}
		OutFirst = { { ECellCorner::SouthWest, ECellCorner::NorthEast, ECellCorner::SouthEast } };
		OutSecond = { { ECellCorner::SouthWest, ECellCorner::NorthWest, ECellCorner::NorthEast } };
	}

	FVector CellCornerOffset(int32 Code, ECellCorner Corner)
	{
		const int32 Index = static_cast<int32>(Corner);
		const int32 CornerX = CornerOffsets[Index][0];
		const int32 CornerY = CornerOffsets[Index][1];
		return FVector(
			CornerX * static_cast<double>(CellSize),
			CornerY * static_cast<double>(CellSize),
			CellCornerHeight(Code, CornerX, CornerY));
	}

	FVector CellTriangleNormal(int32 Code, int32 TriangleIndex)
	{
		FCellTriangle First;
		FCellTriangle Second;
		CellTopTriangles(Code, First, Second);
		const FCellTriangle& Triangle = TriangleIndex == 0 ? First : Second;

		const FVector A = CellCornerOffset(Code, Triangle.Corners[0]);
		const FVector B = CellCornerOffset(Code, Triangle.Corners[1]);
		const FVector C = CellCornerOffset(Code, Triangle.Corners[2]);
		FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();

		// Top faces point up by construction. Resolving the sign here rather than trusting the
		// winding keeps this function independent of the handedness flip described in the
		// header, so it stays correct whichever order the mesh builder settles on.
		if (Normal.Z < 0.0)
		{
			Normal = -Normal;
		}
		return Normal;
	}

	bool CellDiagonalIsCrease(int32 Code)
	{
		return IsCornerShape(CellShape(Code));
	}

	void CellDiagonalCorners(int32 Code, ECellCorner& OutFrom, ECellCorner& OutTo)
	{
		if (CellDiagonal(CellShape(Code)) == ECellDiagonal::NorthWestSouthEast)
		{
			OutFrom = ECellCorner::NorthWest;
			OutTo = ECellCorner::SouthEast;
			return;
		}
		OutFrom = ECellCorner::SouthWest;
		OutTo = ECellCorner::NorthEast;
	}

	void CellEdgeCorners(ECellDirection Direction, ECellCorner& OutFirst, ECellCorner& OutSecond)
	{
		switch (Direction)
		{
		case ECellDirection::North:
			OutFirst = ECellCorner::NorthWest;
			OutSecond = ECellCorner::NorthEast;
			return;
		case ECellDirection::East:
			OutFirst = ECellCorner::SouthEast;
			OutSecond = ECellCorner::NorthEast;
			return;
		case ECellDirection::South:
			OutFirst = ECellCorner::SouthWest;
			OutSecond = ECellCorner::SouthEast;
			return;
		default:
			OutFirst = ECellCorner::SouthWest;
			OutSecond = ECellCorner::NorthWest;
			return;
		}
	}

	ECellDirection OppositeDirection(ECellDirection Direction)
	{
		switch (Direction)
		{
		case ECellDirection::North:
			return ECellDirection::South;
		case ECellDirection::East:
			return ECellDirection::West;
		case ECellDirection::South:
			return ECellDirection::North;
		default:
			return ECellDirection::East;
		}
	}

	int32 CellEdgeTriangleIndex(int32 Code, ECellDirection Direction)
	{
		// With the south-west/north-east split the first triangle covers the south-east half,
		// so it owns the south and east edges; the north-west/south-east split moves the east
		// edge to the second triangle and the west edge to the first.
		if (CellDiagonal(CellShape(Code)) == ECellDiagonal::NorthWestSouthEast)
		{
			return (Direction == ECellDirection::South || Direction == ECellDirection::West) ? 0 : 1;
		}
		return (Direction == ECellDirection::South || Direction == ECellDirection::East) ? 0 : 1;
	}

	EEdgeInk EdgeInkBetween(int32 CodeA, int32 CodeB, ECellDirection DirectionFromA)
	{
		ECellCorner FirstA;
		ECellCorner SecondA;
		CellEdgeCorners(DirectionFromA, FirstA, SecondA);

		const ECellDirection DirectionFromB = OppositeDirection(DirectionFromA);
		ECellCorner FirstB;
		ECellCorner SecondB;
		CellEdgeCorners(DirectionFromB, FirstB, SecondB);

		// CellEdgeCorners orders both edges west-to-east or south-to-north, so the two lists
		// already name the same two world points in the same order.
		const bool bHeightsMatch =
			FMath::IsNearlyEqual(CellCornerHeight(CodeA, FirstA), CellCornerHeight(CodeB, FirstB))
			&& FMath::IsNearlyEqual(CellCornerHeight(CodeA, SecondA), CellCornerHeight(CodeB, SecondB));
		if (!bHeightsMatch)
		{
			return EEdgeInk::Cliff;
		}

		const FVector NormalA = CellTriangleNormal(CodeA, CellEdgeTriangleIndex(CodeA, DirectionFromA));
		const FVector NormalB = CellTriangleNormal(CodeB, CellEdgeTriangleIndex(CodeB, DirectionFromB));
		return NormalA.Equals(NormalB, NormalMatchTolerance) ? EEdgeInk::None : EEdgeInk::Fold;
	}
}
