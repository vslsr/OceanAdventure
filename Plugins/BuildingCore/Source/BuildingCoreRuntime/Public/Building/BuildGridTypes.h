// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BuildGridTypes.generated.h"

UENUM(BlueprintType)
enum class EBuildSlotType : uint8
{
	/** Load-bearing base cell. */
	Foundation,
	/** Walkable surface. */
	Floor,
	/** Occupies one edge of a cell. Low walls only: a TopDown camera cannot see past a tall one. */
	Wall,
	/**
	 * Something hung on a wall edge. Deliberately a separate slot from Wall so a wall and its
	 * decoration can coexist on the same edge instead of competing for one key.
	 */
	WallProp,
	/** Something standing on a floor, positioned by SubCell. */
	Prop
	// No Roof: the camera is a fixed TopDown angle, so a ceiling would hide the player.
};

USTRUCT(BlueprintType)
struct BUILDINGCORERUNTIME_API FBuildGridCoord
{
	GENERATED_BODY()

	FBuildGridCoord() = default;
	FBuildGridCoord(int32 InX, int32 InY, int32 InLevel)
		: X(InX), Y(InY), Level(InLevel)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	int32 X = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	int32 Y = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	int32 Level = 0;

	bool operator==(const FBuildGridCoord& Other) const
	{
		return X == Other.X && Y == Other.Y && Level == Other.Level;
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("(%d,%d,L%d)"), X, Y, Level);
	}

	friend uint32 GetTypeHash(const FBuildGridCoord& Coord)
	{
		return HashCombine(
			HashCombine(::GetTypeHash(Coord.X), ::GetTypeHash(Coord.Y)),
			::GetTypeHash(Coord.Level));
	}
};

USTRUCT(BlueprintType)
struct BUILDINGCORERUNTIME_API FBuildSlotKey
{
	GENERATED_BODY()

	/** Dense, editor-style in-cell snap grid for props. */
	static constexpr uint8 SubCellSide = 9;
	static constexpr uint8 SubCellCount = SubCellSide * SubCellSide;
	static constexpr uint8 CenterSubCell = SubCellCount / 2;

	FBuildSlotKey() = default;
	FBuildSlotKey(
		const FBuildGridCoord& InCoord,
		EBuildSlotType InSlot,
		uint8 InEdgeIndex = 0,
		uint8 InSubCell = FBuildSlotKey::CenterSubCell)
		: Coord(InCoord)
		, Slot(InSlot)
		// Each field is normalised to zero where it carries no meaning, so two keys that
		// describe the same slot always hash and compare equal.
		, EdgeIndex(UsesEdgeIndex(InSlot) ? (InEdgeIndex & 3) : 0)
		, SubCell(UsesSubCell(InSlot)
			? FMath::Min<uint8>(InSubCell, SubCellCount - 1)
			: CenterSubCell)
	{
	}

	static bool UsesEdgeIndex(EBuildSlotType Slot)
	{
		return Slot == EBuildSlotType::Wall || Slot == EBuildSlotType::WallProp;
	}

	static bool UsesSubCell(EBuildSlotType Slot)
	{
		return Slot == EBuildSlotType::Prop;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	FBuildGridCoord Coord;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	EBuildSlotType Slot = EBuildSlotType::Floor;

	/** Wall and WallProp only: 0=+X, 1=+Y, 2=-X, 3=-Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	uint8 EdgeIndex = 0;

	/** Prop only: position inside the 9x9 snap grid, row-major with 40 at the centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	uint8 SubCell = FBuildSlotKey::CenterSubCell;

	bool operator==(const FBuildSlotKey& Other) const
	{
		return Coord == Other.Coord
			&& Slot == Other.Slot
			&& EdgeIndex == Other.EdgeIndex
			&& SubCell == Other.SubCell;
	}

	friend uint32 GetTypeHash(const FBuildSlotKey& Key)
	{
		return HashCombine(
			HashCombine(
				HashCombine(GetTypeHash(Key.Coord), ::GetTypeHash(static_cast<uint8>(Key.Slot))),
				::GetTypeHash(Key.EdgeIndex)),
			::GetTypeHash(Key.SubCell));
	}
};

USTRUCT(BlueprintType)
struct BUILDINGCORERUNTIME_API FBuildGridSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "10.0", Units = "cm"))
	double CellSize = 200.0;

	/**
	 * Optional rectangular-cell override. A non-positive axis falls back to CellSize, which
	 * preserves existing square-grid hosts while allowing rectangular modular hosts such as
	 * a complete raft section.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (Units = "cm"))
	FVector2D CellSizeXY = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "10.0", Units = "cm"))
	double LevelHeight = 250.0;

	/**
	 * Local-space XY offset of the grid lines. Hosts set this so whole cells line up with
	 * their buildable area: an odd cell count needs a half-cell shift to stay centred on
	 * the host origin, an even count does not.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (Units = "cm"))
	FVector2D CellOrigin = FVector2D::ZeroVector;

	/** Local-space Z of level 0's walking surface, e.g. the raft deck's top face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (Units = "cm"))
	double BaseHeight = 0.0;
};

namespace BuildGrid
{
	inline FVector2D GetCellSize(const FBuildGridSettings& Settings)
	{
		const double SquareCellSize = FMath::Max(10.0, Settings.CellSize);
		return FVector2D(
			Settings.CellSizeXY.X > 0.0 ? FMath::Max(10.0, Settings.CellSizeXY.X) : SquareCellSize,
			Settings.CellSizeXY.Y > 0.0 ? FMath::Max(10.0, Settings.CellSizeXY.Y) : SquareCellSize);
	}

	inline double GetMinCellSize(const FBuildGridSettings& Settings)
	{
		const FVector2D CellSize = GetCellSize(Settings);
		return FMath::Min(CellSize.X, CellSize.Y);
	}

	inline double GetMaxCellSize(const FBuildGridSettings& Settings)
	{
		const FVector2D CellSize = GetCellSize(Settings);
		return FMath::Max(CellSize.X, CellSize.Y);
	}

	inline FBuildGridCoord LocalToCoord(const FVector& Local, const FBuildGridSettings& Settings)
	{
		const FVector2D CellSize = GetCellSize(Settings);
		const double LevelHeight = FMath::Max(10.0, Settings.LevelHeight);
		return FBuildGridCoord(
			FMath::FloorToInt((Local.X - Settings.CellOrigin.X) / CellSize.X),
			FMath::FloorToInt((Local.Y - Settings.CellOrigin.Y) / CellSize.Y),
			FMath::FloorToInt((Local.Z - Settings.BaseHeight) / LevelHeight));
	}

	inline FVector CoordToLocalCenter(const FBuildGridCoord& Coord, const FBuildGridSettings& Settings)
	{
		const FVector2D CellSize = GetCellSize(Settings);
		return FVector(
			Settings.CellOrigin.X + (Coord.X + 0.5) * CellSize.X,
			Settings.CellOrigin.Y + (Coord.Y + 0.5) * CellSize.Y,
			Settings.BaseHeight + Coord.Level * Settings.LevelHeight);
	}

	inline FVector EdgeOffset(uint8 EdgeIndex, const FBuildGridSettings& Settings)
	{
		const FVector2D CellSize = GetCellSize(Settings);
		switch (EdgeIndex & 3)
		{
		case 0: return FVector(CellSize.X * 0.5, 0.0, 0.0);
		case 1: return FVector(0.0, CellSize.Y * 0.5, 0.0);
		case 2: return FVector(-CellSize.X * 0.5, 0.0, 0.0);
		default: return FVector(0.0, -CellSize.Y * 0.5, 0.0);
		}
	}

	inline float EdgeYaw(uint8 EdgeIndex)
	{
		return 90.0f * (EdgeIndex & 3);
	}

	/** Offset from the cell centre for one point on the dense in-cell prop snap grid. */
	inline FVector SubCellOffset(uint8 SubCell, const FBuildGridSettings& Settings)
	{
		const FVector2D CellSize = GetCellSize(Settings);
		const int32 Clamped = FMath::Clamp<int32>(
			SubCell, 0, FBuildSlotKey::SubCellCount - 1);
		const int32 HalfSide = FBuildSlotKey::SubCellSide / 2;
		// Dividing by the point count leaves half a snap step at the cell edge, so
		// furniture on adjacent cells does not touch.
		const double OffsetX = (Clamped % FBuildSlotKey::SubCellSide - HalfSide)
			* CellSize.X / FBuildSlotKey::SubCellSide;
		const double OffsetY = (Clamped / FBuildSlotKey::SubCellSide - HalfSide)
			* CellSize.Y / FBuildSlotKey::SubCellSide;
		return FVector(OffsetX, OffsetY, 0.0);
	}

	/** Nearest in-cell prop position to an offset measured from the cell centre. */
	inline uint8 FindNearestSubCell(const FVector& OffsetFromCenter, const FBuildGridSettings& Settings)
	{
		const FVector2D CellSize = GetCellSize(Settings);
		const int32 HalfSide = FBuildSlotKey::SubCellSide / 2;
		const double StepX = CellSize.X / FBuildSlotKey::SubCellSide;
		const double StepY = CellSize.Y / FBuildSlotKey::SubCellSide;
		const int32 Column = FMath::Clamp(
			FMath::RoundToInt(OffsetFromCenter.X / StepX), -HalfSide, HalfSide);
		const int32 Row = FMath::Clamp(
			FMath::RoundToInt(OffsetFromCenter.Y / StepY), -HalfSide, HalfSide);
		return static_cast<uint8>(
			(Row + HalfSide) * FBuildSlotKey::SubCellSide + Column + HalfSide);
	}

	/** Same-level 4-neighbours only; used for snapping and support checks. */
	inline void GetPlanarNeighbors(const FBuildGridCoord& Coord, TArray<FBuildGridCoord>& OutNeighbors)
	{
		OutNeighbors.Reset(4);
		OutNeighbors.Add({Coord.X + 1, Coord.Y, Coord.Level});
		OutNeighbors.Add({Coord.X - 1, Coord.Y, Coord.Level});
		OutNeighbors.Add({Coord.X, Coord.Y + 1, Coord.Level});
		OutNeighbors.Add({Coord.X, Coord.Y - 1, Coord.Level});
	}

	inline void GetNeighbors(const FBuildGridCoord& Coord, TArray<FBuildGridCoord>& OutNeighbors)
	{
		OutNeighbors.Reset(6);
		OutNeighbors.Add({Coord.X + 1, Coord.Y, Coord.Level});
		OutNeighbors.Add({Coord.X - 1, Coord.Y, Coord.Level});
		OutNeighbors.Add({Coord.X, Coord.Y + 1, Coord.Level});
		OutNeighbors.Add({Coord.X, Coord.Y - 1, Coord.Level});
		OutNeighbors.Add({Coord.X, Coord.Y, Coord.Level + 1});
		OutNeighbors.Add({Coord.X, Coord.Y, Coord.Level - 1});
	}
}
