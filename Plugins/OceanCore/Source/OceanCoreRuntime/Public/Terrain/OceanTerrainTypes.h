// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "OceanTerrainTypes.generated.h"

/**
 * Stepped-terrain constants and the packed cell code.
 *
 * Ported from SkyLand shared/world/terrainConfig.mjs. See
 * doc/tech/SkyLand_地形系统迁移方案.md for the full migration plan; this header is §4.1.
 *
 * The terrain is a grid of two-metre cells, each sitting on an integer height level one
 * metre apart. A cell is never interpolated against its neighbours: it is flat, or it is
 * one of twelve fixed ramp/corner shapes that connect it to a neighbour exactly one level
 * higher. That closed shape set is what makes the line-art style cheap here -- the creases
 * are known from the enum instead of extracted from topology (see OceanTerrainOutline.h).
 *
 * Reflection enums are declared so the editor and Blueprints can display a cell, but every
 * function in this layer is a free function over plain integers. Nothing below
 * OceanTerrainMeshBuilder touches UObject, UWorld, or any engine state -- that is what lets
 * the parity test compare it cell-for-cell against the reference implementation.
 *
 * AXIS MAPPING (migration plan §5, decision three). SkyLand is right-handed Y-up with
 * +X = EAST and +Z = NORTH; Unreal is left-handed Z-up. The mapping used here is:
 *
 *     SkyLand (x, y, z)  ->  Unreal (X, Y, Z) = (x, z, y)
 *         +X east        ->  +X east
 *         +Z north       ->  +Y north
 *         +Y up          ->  +Z up
 *
 * so cell coordinates carry across with no index swap and the parity fixture can be read
 * directly. The compass names below therefore mean NORTH = Unreal +Y and EAST = Unreal +X,
 * which is not Unreal's usual "+X is forward" convention -- they are world compass names,
 * and keeping them aligned with the reference implementation is worth more than matching a
 * convention nothing else here depends on.
 *
 * The mapping swaps two axes, so it flips handedness. Triangle winding must be reversed
 * when this data is turned into mesh index buffers; OceanTerrainOutline::CellTopTriangles
 * documents where.
 */

/** Whether a cell is dry ground or part of a connected body of water. */
UENUM(BlueprintType)
enum class EOceanTerrainSurface : uint8
{
	Ground = 0,
	Water = 1,
};

/**
 * Ground cover. Describes what a cell is made of and nothing else: biome never affects
 * height, shape, or walkability, so a flooded cell keeps the biome of the region it sits
 * in and drains back to the same ground.
 *
 * The numbers are written into the cell code and must not be reordered.
 */
UENUM(BlueprintType)
enum class EOceanTerrainBiome : uint8
{
	Grassland = 0,
	Sand = 1,
	Mud = 2,
	Snow = 3,
	Rock = 4,
};

/**
 * Cell shape. Direction names the high edge of a ramp, or the named corner:
 * CornerHigh* has only the named corner on the upper level, CornerLow* has only the named
 * corner on the lower level.
 */
UENUM(BlueprintType)
enum class EOceanTerrainShape : uint8
{
	Flat = 0,
	RampNorth = 1,
	RampEast = 2,
	RampSouth = 3,
	RampWest = 4,
	CornerHighNorthEast = 5,
	CornerHighSouthEast = 6,
	CornerHighSouthWest = 7,
	CornerHighNorthWest = 8,
	CornerLowNorthEast = 9,
	CornerLowSouthEast = 10,
	CornerLowSouthWest = 11,
	CornerLowNorthWest = 12,
};

namespace OceanTerrain
{
	/** Number of biomes. The code reserves three bits, so at most eight fit. */
	inline constexpr int32 BiomeCount = 5;

	/** Number of shapes, Flat included. */
	inline constexpr int32 ShapeCount = 13;

	/**
	 * Edge length of one editable cell, in centimetres.
	 *
	 * SkyLand works in metres throughout; Unreal works in centimetres. Every length in this
	 * layer is already converted -- two metres is 200 units, one height step is 100 units.
	 * The slope ratio is unit-free and therefore unchanged: one step over one cell is about
	 * 26.6 degrees.
	 */
	inline constexpr float CellSize = 200.0f;

	/** Height of one terrace step, in centimetres. */
	inline constexpr float HeightStep = 100.0f;

	/**
	 * Cells per chunk edge (migration plan §5, decision two, provisional at 32 = 64 m).
	 *
	 * Nothing in the truth layer reads this -- cell codes are a function of global cell
	 * coordinates alone -- so changing it cannot move the terrain. It is chunk addressing
	 * only, and it must stay in step with UOceanWorldManagerComponent's ChunkSize:
	 * ChunkSize == ChunkGrid * CellSize. If those two disagree the chunk grid and the cell
	 * grid slide apart and the whole world renders offset.
	 */
	inline constexpr int32 ChunkGrid = 32;

	/** Chunk edge length in centimetres, derived so it cannot drift from ChunkGrid. */
	inline constexpr float ChunkSize = ChunkGrid * CellSize;

	/** Cells in one chunk. */
	inline constexpr int32 ChunkCellCount = ChunkGrid * ChunkGrid;

	/**
	 * Bit layout: shape in the low four bits, surface in bit 4, biome in bits 5-7, signed
	 * height level in the high byte.
	 *
	 * The split is deliberate: the low eight bits (shape + surface + biome) fit a uint8 and
	 * the height fits an int16, which is how a whole chunk compresses to one Int16 array
	 * plus one uint8 array for storage, replication, and editor preview. Keep it.
	 */
	inline constexpr int32 ShapeMask = 0b1111;
	inline constexpr int32 SurfaceShift = 4;
	inline constexpr int32 BiomeShift = 5;
	inline constexpr int32 BiomeMask = 0b111;
	inline constexpr int32 HeightShift = 8;

	/** Height levels the code can hold. The generator only uses -2..2; the rest is for edits. */
	inline constexpr int32 MinimumHeightLevel = -128;
	inline constexpr int32 MaximumHeightLevel = 127;

	/**
	 * Packs the four fields into one integer. The result is always in [0, 0xFFFF].
	 *
	 * Biome has no default on purpose. The reference implementation defaults it to grassland
	 * so hand-written codes in old tests stay valid, but every generate and edit path passes
	 * it explicitly -- without that, raising one cell of snow quietly turns it back into
	 * grass, and the result is a perfectly legal code that no assertion catches.
	 */
	inline constexpr int32 EncodeCell(
		int32 HeightLevel,
		EOceanTerrainSurface Surface,
		EOceanTerrainShape Shape,
		EOceanTerrainBiome Biome)
	{
		const int32 SafeHeight = HeightLevel < MinimumHeightLevel
			? MinimumHeightLevel
			: (HeightLevel > MaximumHeightLevel ? MaximumHeightLevel : HeightLevel);
		return ((SafeHeight & 0xFF) << HeightShift)
			| ((static_cast<int32>(Biome) & BiomeMask) << BiomeShift)
			| ((static_cast<int32>(Surface) & 1) << SurfaceShift)
			| (static_cast<int32>(Shape) & ShapeMask);
	}

	/** Signed height level. The sign extension is the easy half of this layer to get wrong. */
	inline constexpr int32 CellHeightLevel(int32 Code)
	{
		return static_cast<int8>((Code >> HeightShift) & 0xFF);
	}

	inline constexpr EOceanTerrainSurface CellSurface(int32 Code)
	{
		return static_cast<EOceanTerrainSurface>((Code >> SurfaceShift) & 1);
	}

	inline constexpr EOceanTerrainBiome CellBiome(int32 Code)
	{
		return static_cast<EOceanTerrainBiome>((Code >> BiomeShift) & BiomeMask);
	}

	inline constexpr EOceanTerrainShape CellShape(int32 Code)
	{
		return static_cast<EOceanTerrainShape>(Code & ShapeMask);
	}

	/** True for the eight corner shapes, which are the ones whose quad is folded. */
	inline constexpr bool IsCornerShape(EOceanTerrainShape Shape)
	{
		return Shape >= EOceanTerrainShape::CornerHighNorthEast
			&& Shape <= EOceanTerrainShape::CornerLowNorthWest;
	}

	/** Corner index within a cell, ordered south-west, south-east, north-east, north-west. */
	enum class ECellCorner : uint8
	{
		SouthWest = 0,
		SouthEast = 1,
		NorthEast = 2,
		NorthWest = 3,
	};

	/** Cell-edge direction, in the same order the generator scans cardinal neighbours. */
	enum class ECellDirection : uint8
	{
		North = 0,
		East = 1,
		South = 2,
		West = 3,
	};
}
