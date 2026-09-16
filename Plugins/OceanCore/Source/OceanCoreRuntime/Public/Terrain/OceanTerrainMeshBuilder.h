// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Terrain/OceanTerrainOutline.h"
#include "Terrain/OceanTerrainTypes.h"

/**
 * Turns a window of cell codes into one chunk's geometry.
 *
 * Ported from SkyLand shared/world/terrainCollisionMesh.mjs. Migration plan §4.7.
 *
 * ONE IMPLEMENTATION. The reference keeps the render mesh and the collision mesh on a single
 * topology and says why: let the two drift and the client walks on ground that is not the
 * ground it drew. The same applies here -- the fill mesh handed to the renderer and the
 * triangles handed to the body setup come out of one call, and there is no "simplified"
 * variant for either side.
 *
 * INPUT IS PLAIN DATA on purpose. The builder takes a seed and a flat array of edited cells,
 * never a callback into a patch store, because a callback cannot cross a thread boundary and
 * a seed plus a handful of overrides can. That is what lets chunk building run on a task
 * thread without touching a UObject (migration plan §4.8).
 */
namespace OceanTerrain
{
	/**
	 * Edge of the code window: one more than the chunk's cell count, not the same.
	 *
	 * Triangles are laid per cell, but the cliff faces on a cell's east and north sides belong
	 * to that cell, so building the last row needs the first row of the next chunk. The window
	 * is the closed interval [0, ChunkGrid]. Get this wrong and the symptom is a seam of cracks
	 * along every chunk border.
	 */
	inline constexpr int32 ChunkCodeSpan = ChunkGrid + 1;

	/**
	 * Paper tones for the five biomes.
	 *
	 * Deliberately not five absolute colours. The base stays the scene's ground tone and each
	 * biome only shifts it by a weight, so re-toning a map moves all five together and the
	 * world still reads as one place instead of five collaged patches. Ported from SkyLand
	 * src/models/terrain/terrainBiomeStyle.ts.
	 */
	struct OCEANCORERUNTIME_API FTerrainPalette
	{
		/** Scene paper tone everything is mixed from. */
		FLinearColor Ground = FLinearColor(0.82f, 0.78f, 0.70f);

		/** Terrace shading. The top face is flat, so relief reads entirely off these two. */
		float FloorShade = 0.84f;
		float CliffShade = 0.70f;

		/**
		 * Water surface tint.
		 *
		 * Water here is per-cell, not a sheet laid over the world: a cell carries water only
		 * when its code says so, so a lake sits in its basin and dry ground below sea level
		 * stays dry. That is the whole reason the reference implementation reads as terrain
		 * rather than as scenery floating under a plane.
		 */
		FLinearColor Water = FLinearColor(0.16f, 0.52f, 0.60f, 0.72f);

		/** Surface colour of a cell standing above water. */
		FLinearColor TopColor(EOceanTerrainBiome Biome) const;
		/** Surface colour of a submerged bed. */
		FLinearColor FloorColor(EOceanTerrainBiome Biome) const;
		/** Colour of a vertical cliff face. */
		FLinearColor CliffColor(EOceanTerrainBiome Biome) const;
	};

	/**
	 * Fill geometry for one chunk, in chunk-local centimetres.
	 *
	 * Local rather than world so a chunk far from the origin does not lose precision in its
	 * float positions; the actor's own transform puts it back in the world.
	 */
	/** Material slots the builder writes. Ground and water need different shading. */
	enum class ETerrainMaterialSlot : int32
	{
		Ground = 0,
		Water = 1,
	};

	struct OCEANCORERUNTIME_API FTerrainMeshData
	{
		TArray<FVector3f> Positions;
		TArray<FVector3f> Normals;
		TArray<FColor> Colors;
		TArray<uint32> Indices;

		/** One entry per triangle, indexing ETerrainMaterialSlot. */
		TArray<int32> TriangleMaterials;

		int32 TriangleCount() const { return Indices.Num() / 3; }

		void Reset();
	};

	/** One stretch of ink along a cell edge or across a cell's own fold. */
	struct FTerrainInkSegment
	{
		FVector3f Start = FVector3f::ZeroVector;
		FVector3f End = FVector3f::ZeroVector;
		EEdgeInk Kind = EEdgeInk::Fold;
	};

	/**
	 * Ink for one chunk.
	 *
	 * Terrain does NOT get its outline from the inverted hull the props use. A hull extrudes
	 * the whole chunk, so it would draw a line around each chunk border -- a seam that exists
	 * in the streaming grid and nowhere in the world -- while still missing every interior
	 * crease, which is the half that actually makes stepped ground read as stepped. All of
	 * terrain's ink comes from here instead, which is the saving
	 * doc/tech/Line-Art-Style-UE5-Mobile-Rendering.md §2.4 is pointing at.
	 */
	struct OCEANCORERUNTIME_API FTerrainInkData
	{
		TArray<FTerrainInkSegment> Segments;

		void Reset() { Segments.Reset(); }
	};

	/**
	 * Fills the (ChunkGrid + 1)^2 code window for a chunk.
	 *
	 * Overrides is a flat [GlobalCellX, GlobalCellY, Code, ...] triple list; entries outside
	 * the window are skipped, which is normal because an edit near a border belongs to two
	 * chunks' windows. Pass an empty view for an unedited world, which is almost every chunk.
	 */
	OCEANCORERUNTIME_API void BuildChunkCodes(
		uint32 WorldSeed,
		FIntPoint ChunkCoord,
		TArrayView<const int32> Overrides,
		TArray<int32>& OutCodes);

	/**
	 * Two triangles per cell, plus the east- and north-owned cliff faces, plus a water quad at
	 * sea level for every cell whose code says it carries water.
	 *
	 * The water is part of this mesh rather than a separate plane over the world. A flat sheet
	 * cannot express "this basin holds water and that hollow does not", and it hides every
	 * terrace that sits at or below its own height -- which, since ordinary ground starts at
	 * level zero and sea level is zero, is most of the world.
	 *
	 * WINDING. Triangles are emitted so that cross(B - A, C - A) points along the face's
	 * outward normal, and the matching normal is written per vertex. This is where the
	 * handedness flip described in OceanTerrainOutline.h is resolved -- the builder never
	 * copies the reference implementation's corner order blindly, it orients each triangle
	 * from geometry. Shading is therefore correct regardless; if back-face culling comes out
	 * inverted in the editor, the fix is one flip here and nowhere else.
	 */
	OCEANCORERUNTIME_API void BuildChunkMesh(
		FIntPoint ChunkCoord,
		TArrayView<const int32> Codes,
		const FTerrainPalette& Palette,
		double SeaLevel,
		FTerrainMeshData& OutMesh);

	/**
	 * Ink segments for one chunk: each cell's own fold, plus its east and north edges.
	 *
	 * A cell owns its east and north edges, the same convention the cliff faces use, so no
	 * edge is drawn twice. A cliff contributes two segments -- the top lip and the foot --
	 * because both are places a vertical face meets a surface.
	 */
	OCEANCORERUNTIME_API void BuildChunkInk(
		FIntPoint ChunkCoord,
		TArrayView<const int32> Codes,
		FTerrainInkData& OutInk);
}
