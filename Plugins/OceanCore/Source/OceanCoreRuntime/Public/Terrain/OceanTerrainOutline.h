// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Terrain/OceanTerrainTypes.h"

/**
 * Where the ink goes, derived from the shape enum alone.
 *
 * This is doc/tech/Line-Art-Style-UE5-Mobile-Rendering.md §2.4 made concrete. The line-art
 * style draws a flat fill plus a second inverted-hull pass, and an inverted hull only ever
 * produces a silhouette: it cannot draw the crease where a flat cell meets a ramp, or the
 * fold across a corner cell. Those interior lines are exactly what makes stepped terrain
 * read as stepped terrain, so without them the ground flattens into a single coloured shape
 * with an outline around the horizon.
 *
 * The usual fix is a Sobel post-process over depth and normals, which on mobile forward
 * means writing a normal render target and paying another full-screen pass. Terrain does
 * not need it: the shape set is closed at thirteen entries, so every crease is already known
 * from two cell codes and no topology extraction runs at all. That is the cheapest line in
 * the whole style, and it is why the shape enum is worth porting verbatim.
 *
 * Consumers:
 *   - the mesh builder (P2) uses CellTopTriangles for the fill mesh, so fill and collision
 *     share one topology;
 *   - the crease queries feed the edge strip that draws interior lines the hull misses.
 *
 * WINDING. CellTopTriangles returns the reference implementation's corner order, which is
 * front-facing in SkyLand's right-handed Y-up frame. The axis mapping in OceanTerrainTypes.h
 * swaps two axes and therefore flips handedness, so a mesh builder emitting these indices
 * unchanged gets inward-facing triangles. Reverse each triangle's winding at the point where
 * indices are written into a buffer, and keep the reversal in one place. CellTriangleNormal
 * below does not depend on that choice -- it always returns the upward-facing normal.
 */
namespace OceanTerrain
{
	/** One top-face triangle, as corner indices into the cell. */
	struct FCellTriangle
	{
		ECellCorner Corners[3];
	};

	/** Which way a cell's quad is split. */
	enum class ECellDiagonal : uint8
	{
		/** South-west to north-east. The default. */
		SouthWestNorthEast = 0,
		/** North-west to south-east, used by the four corner shapes that fold that way. */
		NorthWestSouthEast = 1,
	};

	/** Ink along one cell edge. */
	enum class EEdgeInk : uint8
	{
		/** The two cells are coplanar across this edge: no line, and no cliff geometry. */
		None = 0,
		/** Same height, different slope -- a crease the inverted hull cannot draw. */
		Fold = 1,
		/** The corners differ in height: a vertical face, and the strongest line on the map. */
		Cliff = 2,
	};

	/** Split direction for a shape. */
	ECellDiagonal CellDiagonal(EOceanTerrainShape Shape);

	/**
	 * The two top-face triangles of a cell, in the reference implementation's order.
	 * See the winding note above before writing these into an index buffer.
	 */
	void CellTopTriangles(int32 Code, FCellTriangle& OutFirst, FCellTriangle& OutSecond);

	/** Corner-local position of a corner within its cell, centimetres, Unreal axes. */
	FVector CellCornerOffset(int32 Code, ECellCorner Corner);

	/** Upward-facing unit normal of one of the cell's two triangles. Index is 0 or 1. */
	FVector CellTriangleNormal(int32 Code, int32 TriangleIndex);

	/**
	 * Whether the cell's own diagonal is a crease.
	 *
	 * True for the eight corner shapes and nothing else: a flat cell is planar, and so is
	 * every ramp -- its four corner heights still lie in one plane. Only a corner shape lifts
	 * or drops a single corner, which folds the quad along whichever diagonal CellDiagonal
	 * picked.
	 */
	bool CellDiagonalIsCrease(int32 Code);

	/** The two corners the diagonal runs between. */
	void CellDiagonalCorners(int32 Code, ECellCorner& OutFrom, ECellCorner& OutTo);

	/** The two corners of a cell that lie on one of its edges, in west-to-east / south-to-north order. */
	void CellEdgeCorners(ECellDirection Direction, ECellCorner& OutFirst, ECellCorner& OutSecond);

	/** The opposite edge, i.e. the same world edge seen from the neighbouring cell. */
	ECellDirection OppositeDirection(ECellDirection Direction);

	/** Which of a cell's two triangles touches the given edge. Returns 0 or 1. */
	int32 CellEdgeTriangleIndex(int32 Code, ECellDirection Direction);

	/**
	 * Ink along the edge between a cell and the neighbour in the given direction.
	 *
	 * CodeB must be the cell at (CellX, CellY) offset by Direction; passing an unrelated
	 * neighbour produces a well-formed answer about geometry that does not exist.
	 */
	EEdgeInk EdgeInkBetween(int32 CodeA, int32 CodeB, ECellDirection DirectionFromA);
}
