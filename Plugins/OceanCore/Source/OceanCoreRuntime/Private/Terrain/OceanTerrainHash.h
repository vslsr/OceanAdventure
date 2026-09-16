// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Integer hash and value noise for world generation.
 *
 * Ported from SkyLand shared/world/hash.mjs, whose opening comment is the reason this file
 * looks the way it does:
 *
 *     No floating point is used anywhere here: every intermediate is a 32-bit integer, and
 *     JavaScript's Math.imul / >>> are bit-for-bit equivalent to Rust's wrapping_mul / >>,
 *     so one seed produces the same world in the browser, in the room process, and in WASM.
 *     The moment floating point enters, the two sides can disagree -- you see a tree, I do
 *     not.
 *
 * That argument carries over unchanged to a client and a dedicated server running the same
 * C++: uint32 multiplication wraps by definition, so this port is exact. Do not replace any
 * of it with FMath::PerlinNoise2D or FRandomStream -- the first is floating point and the
 * second has different sequence semantics; either one throws the determinism away.
 *
 * Private on purpose: callers want CellCodeAt and BiomeAt, not the mixer underneath.
 */
namespace OceanTerrain
{
	/** Four-way 32-bit mix. */
	uint32 Hash32(uint32 Seed, int32 A, int32 B, int32 C);

	/** Upper bound of a value-noise sample. Results land in [0, NoiseScale]. */
	inline constexpr int32 NoiseScale = 255;

	/**
	 * Integer bilinear value noise.
	 *
	 * Shift sets the feature size: lattice points sit 2^Shift input units apart. Keep Shift
	 * at 6 or below -- above that the intermediate products overflow 32 bits, which is why
	 * the reference implementation says so and why ValueNoise checks it.
	 */
	int32 ValueNoise(uint32 Seed, int32 X, int32 Y, int32 Shift);
}
