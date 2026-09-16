// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/OceanTerrainMeshBuilder.h"

#include "Terrain/OceanTerrainContent.h"
#include "Terrain/OceanTerrainWater.h"

namespace OceanTerrain
{
	namespace
	{
		/** Per-biome shift away from the scene's paper tone. */
		struct FBiomeTone
		{
			FLinearColor Target;
			float Weight;
		};

		const FBiomeTone BiomeTones[BiomeCount] = {
			{ FLinearColor(0.616f, 0.741f, 0.447f), 0.42f }, // Grassland 0x9dbd72
			{ FLinearColor(0.910f, 0.757f, 0.475f), 0.55f }, // Sand      0xe8c179
			{ FLinearColor(0.612f, 0.541f, 0.408f), 0.46f }, // Mud       0x9c8a68
			{ FLinearColor(0.949f, 0.969f, 0.984f), 0.78f }, // Snow      0xf2f7fb
			{ FLinearColor(0.612f, 0.604f, 0.580f), 0.55f }, // Rock      0x9c9a94
		};

		/**
		 * Positions land on exact grid points -- multiples of CellSize horizontally and of
		 * HeightStep vertically -- so quantising the dedup key is lossless rather than a
		 * tolerance. Two corners that should weld always produce the same key, and two that
		 * should not never collide.
		 */
		constexpr float VertexQuantum = 16.0f;

		struct FVertexKey
		{
			int32 X;
			int32 Y;
			int32 Z;
			int32 NormalX;
			int32 NormalY;
			int32 NormalZ;
			uint32 Color;

			bool operator==(const FVertexKey& Other) const
			{
				return X == Other.X && Y == Other.Y && Z == Other.Z
					&& NormalX == Other.NormalX && NormalY == Other.NormalY && NormalZ == Other.NormalZ
					&& Color == Other.Color;
			}
		};

		uint32 GetTypeHash(const FVertexKey& Key)
		{
			uint32 Hash = ::GetTypeHash(Key.X);
			Hash = HashCombine(Hash, ::GetTypeHash(Key.Y));
			Hash = HashCombine(Hash, ::GetTypeHash(Key.Z));
			Hash = HashCombine(Hash, ::GetTypeHash(Key.NormalX));
			Hash = HashCombine(Hash, ::GetTypeHash(Key.NormalY));
			Hash = HashCombine(Hash, ::GetTypeHash(Key.NormalZ));
			return HashCombine(Hash, ::GetTypeHash(Key.Color));
		}

		FVertexKey MakeVertexKey(const FVector3f& Position, const FVector3f& Normal, const FColor& Color)
		{
			return FVertexKey{
				FMath::RoundToInt(Position.X * VertexQuantum),
				FMath::RoundToInt(Position.Y * VertexQuantum),
				FMath::RoundToInt(Position.Z * VertexQuantum),
				FMath::RoundToInt(Normal.X * 4096.0f),
				FMath::RoundToInt(Normal.Y * 4096.0f),
				FMath::RoundToInt(Normal.Z * 4096.0f),
				Color.ToPackedARGB() };
		}

		/** Welds vertices that agree on position, normal, and colour; splits them otherwise. */
		class FVertexWelder
		{
		public:
			explicit FVertexWelder(FTerrainMeshData& InMesh) : Mesh(InMesh) {}

			uint32 Add(const FVector3f& Position, const FVector3f& Normal, const FColor& Color)
			{
				const FVertexKey Key = MakeVertexKey(Position, Normal, Color);
				if (const uint32* Existing = Lookup.Find(Key))
				{
					return *Existing;
				}
				const uint32 Index = static_cast<uint32>(Mesh.Positions.Num());
				Mesh.Positions.Add(Position);
				Mesh.Normals.Add(Normal);
				Mesh.Colors.Add(Color);
				Lookup.Add(Key, Index);
				return Index;
			}

		private:
			FTerrainMeshData& Mesh;
			TMap<FVertexKey, uint32> Lookup;
		};

		/**
		 * Emits one triangle oriented by geometry rather than by copied corner order.
		 *
		 * OutwardHint only has to point roughly the right way -- up for a top face, along the
		 * step for a cliff. The triangle's own plane supplies the exact normal.
		 */
		void AppendTriangle(
			FTerrainMeshData& Mesh,
			FVertexWelder& Welder,
			const FVector3f& A,
			const FVector3f& B,
			const FVector3f& C,
			const FVector3f& OutwardHint,
			const FColor& Color,
			ETerrainMaterialSlot Slot = ETerrainMaterialSlot::Ground)
		{
			FVector3f Normal = FVector3f::CrossProduct(B - A, C - A).GetSafeNormal();
			if (Normal.IsNearlyZero())
			{
				// Degenerate. A cliff face whose two corners meet at a point produces these, and
				// they would otherwise weld into an index triple that draws nothing but still
				// costs a triangle in every buffer downstream.
				return;
			}

			const bool bFlip = FVector3f::DotProduct(Normal, OutwardHint) < 0.0f;
			if (bFlip)
			{
				Normal = -Normal;
			}

			// Winding, resolved in the engine rather than reasoned about (2026-09-16). Unreal is
			// left-handed and treats clockwise-as-seen-from-the-front as the front face, which
			// is cross(C - A, B - A) -- the opposite of the right-hand rule used to pick Normal
			// above. Emitting A, B, C put every front face downwards: the terrain was invisible
			// from above and visible from underwater, which is what the screenshots showed.
			//
			// This is the single flip the whole pipeline needs; CellTriangleNormal and the ink
			// segments do not depend on it. If it ever inverts again, it inverts here.
			const uint32 IndexA = Welder.Add(A, Normal, Color);
			const uint32 IndexB = Welder.Add(bFlip ? B : C, Normal, Color);
			const uint32 IndexC = Welder.Add(bFlip ? C : B, Normal, Color);
			Mesh.Indices.Add(IndexA);
			Mesh.Indices.Add(IndexB);
			Mesh.Indices.Add(IndexC);
			Mesh.TriangleMaterials.Add(static_cast<int32>(Slot));
		}

		/** Chunk-local position of one corner of one cell. */
		FVector3f CornerPosition(int32 Code, int32 LocalX, int32 LocalY, ECellCorner Corner)
		{
			const int32 Index = static_cast<int32>(Corner);
			const int32 CornerX = (Index == 1 || Index == 2) ? 1 : 0;
			const int32 CornerY = (Index == 2 || Index == 3) ? 1 : 0;
			return FVector3f(
				static_cast<float>((LocalX + CornerX) * CellSize),
				static_cast<float>((LocalY + CornerY) * CellSize),
				static_cast<float>(CellCornerHeight(Code, CornerX, CornerY)));
		}
	}

	FLinearColor FTerrainPalette::TopColor(EOceanTerrainBiome Biome) const
	{
		const int32 Index = FMath::Clamp(static_cast<int32>(Biome), 0, BiomeCount - 1);
		const FBiomeTone& Tone = BiomeTones[Index];
		return FMath::Lerp(Ground, Tone.Target, Tone.Weight);
	}

	FLinearColor FTerrainPalette::FloorColor(EOceanTerrainBiome Biome) const
	{
		return TopColor(Biome) * FloorShade;
	}

	FLinearColor FTerrainPalette::CliffColor(EOceanTerrainBiome Biome) const
	{
		return TopColor(Biome) * CliffShade;
	}

	void FTerrainMeshData::Reset()
	{
		Positions.Reset();
		Normals.Reset();
		Colors.Reset();
		Indices.Reset();
		TriangleMaterials.Reset();
	}

	void BuildChunkCodes(
		uint32 WorldSeed,
		FIntPoint ChunkCoord,
		TArrayView<const int32> Overrides,
		TArray<int32>& OutCodes)
	{
		const int32 OriginCellX = ChunkCoord.X * ChunkGrid;
		const int32 OriginCellY = ChunkCoord.Y * ChunkGrid;

		OutCodes.SetNumUninitialized(ChunkCodeSpan * ChunkCodeSpan);
		for (int32 LocalY = 0; LocalY < ChunkCodeSpan; ++LocalY)
		{
			for (int32 LocalX = 0; LocalX < ChunkCodeSpan; ++LocalX)
			{
				OutCodes[LocalY * ChunkCodeSpan + LocalX] =
					CellCodeAt(WorldSeed, OriginCellX + LocalX, OriginCellY + LocalY);
			}
		}

		for (int32 Offset = 0; Offset + 2 < Overrides.Num(); Offset += 3)
		{
			const int32 LocalX = Overrides[Offset] - OriginCellX;
			const int32 LocalY = Overrides[Offset + 1] - OriginCellY;
			// An edit in a neighbouring chunk can fall outside this window; that is expected,
			// not an error, because a border edit belongs to two chunks' windows.
			if (LocalX < 0 || LocalX >= ChunkCodeSpan || LocalY < 0 || LocalY >= ChunkCodeSpan)
			{
				continue;
			}
			OutCodes[LocalY * ChunkCodeSpan + LocalX] = Overrides[Offset + 2];
		}
	}

	void BuildChunkMesh(
		FIntPoint ChunkCoord,
		TArrayView<const int32> Codes,
		const FTerrainPalette& Palette,
		double SeaLevel,
		FTerrainMeshData& OutMesh)
	{
		check(Codes.Num() == ChunkCodeSpan * ChunkCodeSpan);
		OutMesh.Reset();
		FVertexWelder Welder(OutMesh);

		const auto CodeAt = [&Codes](int32 LocalX, int32 LocalY)
		{
			return Codes[LocalY * ChunkCodeSpan + LocalX];
		};

		for (int32 LocalY = 0; LocalY < ChunkGrid; ++LocalY)
		{
			for (int32 LocalX = 0; LocalX < ChunkGrid; ++LocalX)
			{
				const int32 Code = CodeAt(LocalX, LocalY);
				const EOceanTerrainBiome Biome = CellBiome(Code);

				// A submerged bed is shaded down rather than flattened: the terrace reads through
				// the water because the floor and cliff shades carry the relief, not the lighting.
				const bool bSubmerged = CellHasWater(Code, SeaLevel);
				const FColor TopTint =
					(bSubmerged ? Palette.FloorColor(Biome) : Palette.TopColor(Biome)).ToFColor(true);
				const FColor CliffTint = Palette.CliffColor(Biome).ToFColor(true);

				FCellTriangle First;
				FCellTriangle Second;
				CellTopTriangles(Code, First, Second);
				for (const FCellTriangle& Triangle : { First, Second })
				{
					AppendTriangle(
						OutMesh,
						Welder,
						CornerPosition(Code, LocalX, LocalY, Triangle.Corners[0]),
						CornerPosition(Code, LocalX, LocalY, Triangle.Corners[1]),
						CornerPosition(Code, LocalX, LocalY, Triangle.Corners[2]),
						FVector3f::UpVector,
						TopTint);
				}

				// East and north cliffs belong to this cell, which is why the code window carries
				// one extra row and column.
				const int32 EastCode = CodeAt(LocalX + 1, LocalY);
				const FVector3f SouthEast = CornerPosition(Code, LocalX, LocalY, ECellCorner::SouthEast);
				const FVector3f NorthEast = CornerPosition(Code, LocalX, LocalY, ECellCorner::NorthEast);
				const FVector3f EastSouthWest =
					CornerPosition(EastCode, LocalX + 1, LocalY, ECellCorner::SouthWest);
				const FVector3f EastNorthWest =
					CornerPosition(EastCode, LocalX + 1, LocalY, ECellCorner::NorthWest);
				if (SouthEast.Z != EastSouthWest.Z || NorthEast.Z != EastNorthWest.Z)
				{
					// The face looks out from whichever side stands higher.
					const float EastRise = (SouthEast.Z + NorthEast.Z) - (EastSouthWest.Z + EastNorthWest.Z);
					const FVector3f Hint(EastRise > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
					AppendTriangle(OutMesh, Welder, SouthEast, NorthEast, EastNorthWest, Hint, CliffTint);
					AppendTriangle(OutMesh, Welder, SouthEast, EastNorthWest, EastSouthWest, Hint, CliffTint);
				}

				// Water sits at sea level over the cell's own footprint, and only where the code
				// says this cell carries water. CellHasWater also requires a corner below the
				// line, so a bed that has risen clear of the water stops drawing a surface
				// without anyone having to remember to clear the flag.
				if (bSubmerged)
				{
					const float WaterZ = static_cast<float>(SeaLevel);
					const float X0 = static_cast<float>(LocalX * CellSize);
					const float Y0 = static_cast<float>(LocalY * CellSize);
					const float X1 = X0 + CellSize;
					const float Y1 = Y0 + CellSize;
					const FColor WaterTint = Palette.Water.ToFColor(true);
					const FVector3f SouthWestWater(X0, Y0, WaterZ);
					const FVector3f SouthEastWater(X1, Y0, WaterZ);
					const FVector3f NorthEastWater(X1, Y1, WaterZ);
					const FVector3f NorthWestWater(X0, Y1, WaterZ);
					AppendTriangle(OutMesh, Welder, SouthWestWater, NorthEastWater, SouthEastWater,
						FVector3f::UpVector, WaterTint, ETerrainMaterialSlot::Water);
					AppendTriangle(OutMesh, Welder, SouthWestWater, NorthWestWater, NorthEastWater,
						FVector3f::UpVector, WaterTint, ETerrainMaterialSlot::Water);
				}

				const int32 NorthCode = CodeAt(LocalX, LocalY + 1);
				const FVector3f NorthWest = CornerPosition(Code, LocalX, LocalY, ECellCorner::NorthWest);
				const FVector3f NorthSouthWest =
					CornerPosition(NorthCode, LocalX, LocalY + 1, ECellCorner::SouthWest);
				const FVector3f NorthSouthEast =
					CornerPosition(NorthCode, LocalX, LocalY + 1, ECellCorner::SouthEast);
				if (NorthWest.Z != NorthSouthWest.Z || NorthEast.Z != NorthSouthEast.Z)
				{
					const float NorthRise =
						(NorthWest.Z + NorthEast.Z) - (NorthSouthWest.Z + NorthSouthEast.Z);
					const FVector3f Hint(0.0f, NorthRise > 0.0f ? 1.0f : -1.0f, 0.0f);
					AppendTriangle(OutMesh, Welder, NorthWest, NorthSouthWest, NorthSouthEast, Hint, CliffTint);
					AppendTriangle(OutMesh, Welder, NorthWest, NorthSouthEast, NorthEast, Hint, CliffTint);
				}
			}
		}
	}

	void BuildChunkInk(FIntPoint ChunkCoord, TArrayView<const int32> Codes, FTerrainInkData& OutInk)
	{
		check(Codes.Num() == ChunkCodeSpan * ChunkCodeSpan);
		OutInk.Reset();

		const auto CodeAt = [&Codes](int32 LocalX, int32 LocalY)
		{
			return Codes[LocalY * ChunkCodeSpan + LocalX];
		};

		const auto AddSegment =
			[&OutInk](const FVector3f& Start, const FVector3f& End, EEdgeInk Kind)
		{
			if (!Start.Equals(End))
			{
				OutInk.Segments.Add(FTerrainInkSegment{ Start, End, Kind });
			}
		};

		for (int32 LocalY = 0; LocalY < ChunkGrid; ++LocalY)
		{
			for (int32 LocalX = 0; LocalX < ChunkGrid; ++LocalX)
			{
				const int32 Code = CodeAt(LocalX, LocalY);

				if (CellDiagonalIsCrease(Code))
				{
					ECellCorner From;
					ECellCorner To;
					CellDiagonalCorners(Code, From, To);
					AddSegment(
						CornerPosition(Code, LocalX, LocalY, From),
						CornerPosition(Code, LocalX, LocalY, To),
						EEdgeInk::Fold);
				}

				const ECellDirection Directions[2] = { ECellDirection::East, ECellDirection::North };
				const int32 NeighbourOffsets[2][2] = { { 1, 0 }, { 0, 1 } };
				for (int32 Which = 0; Which < 2; ++Which)
				{
					const ECellDirection Direction = Directions[Which];
					const int32 NeighbourX = LocalX + NeighbourOffsets[Which][0];
					const int32 NeighbourY = LocalY + NeighbourOffsets[Which][1];
					const int32 NeighbourCode = CodeAt(NeighbourX, NeighbourY);

					const EEdgeInk Ink = EdgeInkBetween(Code, NeighbourCode, Direction);
					if (Ink == EEdgeInk::None)
					{
						continue;
					}

					ECellCorner NearFirst;
					ECellCorner NearSecond;
					CellEdgeCorners(Direction, NearFirst, NearSecond);
					AddSegment(
						CornerPosition(Code, LocalX, LocalY, NearFirst),
						CornerPosition(Code, LocalX, LocalY, NearSecond),
						Ink);

					if (Ink == EEdgeInk::Cliff)
					{
						// The foot of the cliff is a second line: the vertical face meets a surface
						// at both ends, and drawing only the lip leaves the step looking painted on.
						ECellCorner FarFirst;
						ECellCorner FarSecond;
						CellEdgeCorners(OppositeDirection(Direction), FarFirst, FarSecond);
						AddSegment(
							CornerPosition(NeighbourCode, NeighbourX, NeighbourY, FarFirst),
							CornerPosition(NeighbourCode, NeighbourX, NeighbourY, FarSecond),
							EEdgeInk::Cliff);
					}
				}
			}
		}
	}
}
