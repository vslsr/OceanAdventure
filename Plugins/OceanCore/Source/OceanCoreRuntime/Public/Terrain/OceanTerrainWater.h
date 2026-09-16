// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Terrain/OceanTerrainContent.h"
#include "Terrain/OceanTerrainTypes.h"

/**
 * Water and support queries over a cell code.
 *
 * Ported from SkyLand shared/world/{terrainWater,terrainSupport,terrainMovement}.mjs.
 *
 * The one rule to keep, because it is the opposite of what a continuous height field
 * teaches: a cell's height is always the ground or the sea bed. EOceanTerrainSurface::Water
 * marks a cell as belonging to a connected body of water -- it is not "below sea level".
 * Sea level never edits the bed, and it never floods ordinary ground that happens to sit
 * low. A dry hollow below sea level stays dry.
 *
 * It is an easy rule to optimise away by accident. The symptom is that digging a pit fills
 * it with water, and no assertion anywhere will fire.
 */
namespace OceanTerrain
{
	/** One ten-thousandth of a centimetre: the metre-scale epsilon of the reference, converted. */
	inline constexpr double WaterDepthEpsilon = 1.0e-4;

	/**
	 * Highest of a cell's four corners.
	 *
	 * Foundations sit here, not at the cell centre: a foundation has to cover the whole cell,
	 * and placing it at the centre height sinks half of it into a ramp. Anything building on
	 * terrain should call this rather than deriving its own answer, so both ends agree.
	 */
	double CellTopHeight(int32 Code);

	/** Lowest of a cell's four corners. */
	double CellMinimumBedHeight(int32 Code);

	/** Actual water depth at a sample. Ordinary ground stays dry however low it is. */
	double WaterDepth(const FTerrainSample& Sample, double SeaLevel = 0.0);

	bool SampleHasWater(const FTerrainSample& Sample, double SeaLevel = 0.0);

	/** Visible surface height, shared by traces, camera, and interaction. */
	double SurfaceHeight(const FTerrainSample& Sample, double SeaLevel = 0.0);

	/**
	 * Whether a cell needs a water surface drawn.
	 *
	 * A sloped bed qualifies as soon as any corner is below sea level; the part standing
	 * proud of the water is left to the depth test, which is what lets a sea bed keep using
	 * ramp and corner shapes instead of flattening at the shoreline.
	 */
	bool CellHasWater(int32 Code, double SeaLevel = 0.0);

	/**
	 * Height a mover rests at. Water carries it, unless the bed is higher than where buoyancy
	 * would hold it.
	 *
	 * Buoyancy is an explicit flag rather than a sentinel draft. The reference implementation
	 * tests Number.isFinite on the draft, and every sentinel that survives the port -- a huge
	 * value, a negative one, NaN -- either reads as a real draft somewhere or quietly changes
	 * what a zero draft means.
	 */
	double MovementHeight(
		const FTerrainSample& Sample,
		double SeaLevel = 0.0,
		bool bFloats = false,
		double BuoyancyDraft = 0.0);
}
