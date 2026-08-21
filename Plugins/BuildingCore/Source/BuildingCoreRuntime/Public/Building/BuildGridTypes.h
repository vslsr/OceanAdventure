// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BuildGridTypes.generated.h"

UENUM(BlueprintType)
enum class EBuildSlotType : uint8
{
	Foundation,
	Floor,
	Wall,
	Roof,
	Prop
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

	FBuildSlotKey() = default;
	FBuildSlotKey(const FBuildGridCoord& InCoord, EBuildSlotType InSlot, uint8 InEdgeIndex = 0)
		: Coord(InCoord), Slot(InSlot), EdgeIndex(InSlot == EBuildSlotType::Wall ? (InEdgeIndex & 3) : 0)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	FBuildGridCoord Coord;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	EBuildSlotType Slot = EBuildSlotType::Floor;

	/** Wall only: 0=+X, 1=+Y, 2=-X, 3=-Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	uint8 EdgeIndex = 0;

	bool operator==(const FBuildSlotKey& Other) const
	{
		return Coord == Other.Coord && Slot == Other.Slot && EdgeIndex == Other.EdgeIndex;
	}

	friend uint32 GetTypeHash(const FBuildSlotKey& Key)
	{
		return HashCombine(
			HashCombine(GetTypeHash(Key.Coord), ::GetTypeHash(static_cast<uint8>(Key.Slot))),
			::GetTypeHash(Key.EdgeIndex));
	}
};

USTRUCT(BlueprintType)
struct BUILDINGCORERUNTIME_API FBuildGridSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "10.0", Units = "cm"))
	double CellSize = 200.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "10.0", Units = "cm"))
	double LevelHeight = 250.0;
};

namespace BuildGrid
{
	inline FBuildGridCoord LocalToCoord(const FVector& Local, const FBuildGridSettings& Settings)
	{
		const double CellSize = FMath::Max(10.0, Settings.CellSize);
		const double LevelHeight = FMath::Max(10.0, Settings.LevelHeight);
		return FBuildGridCoord(
			FMath::FloorToInt(Local.X / CellSize),
			FMath::FloorToInt(Local.Y / CellSize),
			FMath::FloorToInt(Local.Z / LevelHeight));
	}

	inline FVector CoordToLocalCenter(const FBuildGridCoord& Coord, const FBuildGridSettings& Settings)
	{
		return FVector(
			(Coord.X + 0.5) * Settings.CellSize,
			(Coord.Y + 0.5) * Settings.CellSize,
			Coord.Level * Settings.LevelHeight);
	}

	inline FVector EdgeOffset(uint8 EdgeIndex, const FBuildGridSettings& Settings)
	{
		switch (EdgeIndex & 3)
		{
		case 0: return FVector(Settings.CellSize * 0.5, 0.0, 0.0);
		case 1: return FVector(0.0, Settings.CellSize * 0.5, 0.0);
		case 2: return FVector(-Settings.CellSize * 0.5, 0.0, 0.0);
		default: return FVector(0.0, -Settings.CellSize * 0.5, 0.0);
		}
	}

	inline float EdgeYaw(uint8 EdgeIndex)
	{
		return 90.0f * (EdgeIndex & 3);
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
