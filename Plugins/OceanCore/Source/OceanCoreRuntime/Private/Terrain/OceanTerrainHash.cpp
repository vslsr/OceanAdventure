// Copyright Epic Games, Inc. All Rights Reserved.

#include "OceanTerrainHash.h"

namespace OceanTerrain
{
	namespace
	{
		/** Stable lattice sample, [0, NoiseScale]. */
		int32 LatticeValue(uint32 Seed, int32 LatticeX, int32 LatticeY)
		{
			return static_cast<int32>(Hash32(Seed, LatticeX, LatticeY, 0x51ed270b) & NoiseScale);
		}

		/**
		 * Fixed-point smoothstep: maps a position in [0, Size) to a weight in the same range
		 * so neighbouring cells meet without a visible kink.
		 *
		 * Both the numerator and Value are non-negative here (Value < Size implies
		 * 3 * Size - 2 * Value > Size > 0), so C++ truncating division matches JavaScript's
		 * `| 0`. That equivalence is the whole reason this stays integer-only, and it stops
		 * holding the moment a caller passes a negative Value.
		 */
		int32 SmoothWeight(int32 Value, int32 Size)
		{
			checkSlow(Value >= 0 && Value < Size);
			const int32 Squared = Value * Value;
			return (3 * Squared * Size - 2 * Squared * Value) / (Size * Size);
		}
	}

	uint32 Hash32(uint32 Seed, int32 A, int32 B, int32 C)
	{
		uint32 Hash = Seed ^ 0x9e3779b9u;
		Hash = (Hash ^ static_cast<uint32>(A)) * 0x85ebca6bu;
		Hash ^= Hash >> 13;
		Hash = (Hash ^ static_cast<uint32>(B)) * 0xc2b2ae35u;
		Hash ^= Hash >> 16;
		Hash = (Hash ^ static_cast<uint32>(C)) * 0x27d4eb2fu;
		Hash ^= Hash >> 15;
		return Hash;
	}

	int32 ValueNoise(uint32 Seed, int32 X, int32 Y, int32 Shift)
	{
		checkSlow(Shift >= 0 && Shift <= 6);
		const int32 Size = 1 << Shift;

		// Arithmetic shift, so negative coordinates floor instead of truncating toward zero;
		// the offsets below are therefore always in [0, Size).
		const int32 LatticeX = X >> Shift;
		const int32 LatticeY = Y >> Shift;
		const int32 WeightX = SmoothWeight(X - (LatticeX << Shift), Size);
		const int32 WeightY = SmoothWeight(Y - (LatticeY << Shift), Size);

		const int32 Corner00 = LatticeValue(Seed, LatticeX, LatticeY);
		const int32 Corner10 = LatticeValue(Seed, LatticeX + 1, LatticeY);
		const int32 Corner01 = LatticeValue(Seed, LatticeX, LatticeY + 1);
		const int32 Corner11 = LatticeValue(Seed, LatticeX + 1, LatticeY + 1);

		const int32 Top = (Corner00 * (Size - WeightX) + Corner10 * WeightX) / Size;
		const int32 Bottom = (Corner01 * (Size - WeightX) + Corner11 * WeightX) / Size;
		return (Top * (Size - WeightY) + Bottom * WeightY) / Size;
	}
}
