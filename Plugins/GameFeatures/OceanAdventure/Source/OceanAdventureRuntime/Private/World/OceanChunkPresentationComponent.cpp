// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/OceanChunkPresentationComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInterface.h"
#include "OceanAdventureRuntimeModule.h"
#include "ProceduralMeshComponent.h"
#include "World/OceanChunkActor.h"
#include "World/OceanGenerationSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanChunkPresentationComponent)

namespace
{
	constexpr float ShorelineMask = 0.02f;
	constexpr float BeachRoughnessScale = 0.2f;

	const TCHAR* TerrainMaterialPath =
		TEXT("/OceanAdventure/Environment/WildOmission/WorldGeneration/M_Terrain.M_Terrain");
	const TCHAR* WaterMeshPath =
		TEXT("/OceanAdventure/Environment/WildOmission/WorldGeneration/SM_Water.SM_Water");
	const TCHAR* WaterMaterialPath =
		TEXT("/OceanAdventure/Environment/WildOmission/WildOmissionCore/Art/Environment/M_Water.M_Water");

	/**
	 * Deterministic 0..1 value for one island cell.
	 *
	 * Deliberately not an engine RNG: height has to stay a pure function of world position
	 * and seed, or two chunks would disagree about the terrain along the edge they share and
	 * the seam would crack open.
	 */
	float HashIslandCell(int32 CellX, int32 CellY, int32 Seed, int32 Salt)
	{
		uint32 Hash = static_cast<uint32>(CellX) * 374761393u
			+ static_cast<uint32>(CellY) * 668265263u
			+ static_cast<uint32>(Seed) * 2246822519u
			+ static_cast<uint32>(Salt) * 3266489917u;
		Hash = (Hash ^ (Hash >> 13)) * 1274126177u;
		Hash ^= Hash >> 16;
		return static_cast<float>(Hash & 0xFFFFFFu) / static_cast<float>(0xFFFFFF);
	}

	float IslandProfileMask(
		float PlateauRadius, float ShorelineRadius, float OceanFloorRadius, float Distance)
	{
		if (Distance <= PlateauRadius)
		{
			return 1.0f;
		}

		if (Distance <= ShorelineRadius)
		{
			const float LandAlpha = FMath::Clamp(
				(Distance - PlateauRadius)
					/ FMath::Max(ShorelineRadius - PlateauRadius, UE_SMALL_NUMBER),
				0.0f,
				1.0f);
			return FMath::Lerp(1.0f, ShorelineMask, LandAlpha);
		}

		const float UnderwaterAlpha = FMath::Clamp(
			(Distance - ShorelineRadius)
				/ FMath::Max(OceanFloorRadius - ShorelineRadius, UE_SMALL_NUMBER),
			0.0f,
			1.0f);
		return FMath::Lerp(ShorelineMask, 0.0f, UnderwaterAlpha);
	}

	float GetIslandShapeHeight(
		float Mask, float OceanFloorHeight, float WaterHeight, float IslandPeakHeight)
	{
		if (Mask <= ShorelineMask)
		{
			const float UnderwaterAlpha = FMath::Clamp(Mask / ShorelineMask, 0.0f, 1.0f);
			const float SmoothUnderwaterAlpha = UnderwaterAlpha * UnderwaterAlpha
				* (3.0f - 2.0f * UnderwaterAlpha);
			return FMath::Lerp(OceanFloorHeight, WaterHeight, SmoothUnderwaterAlpha);
		}

		const float LandAlpha = FMath::Clamp(
			(Mask - ShorelineMask) / (1.0f - ShorelineMask), 0.0f, 1.0f);
		return FMath::Lerp(WaterHeight, IslandPeakHeight, LandAlpha);
	}
}

UOceanChunkPresentationComponent::UOceanChunkPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);

	TerrainMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TerrainMaterialPath));
	WaterMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(WaterMeshPath));
	WaterMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(WaterMaterialPath));
}

void UOceanChunkPresentationComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	AOceanChunkActor* Chunk = Cast<AOceanChunkActor>(GetOwner());
	if (!Chunk)
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("%s must be owned by AOceanChunkActor"), *GetNameSafe(this));
		return;
	}

	Chunk->OnChunkInitialized.AddDynamic(this, &ThisClass::HandleChunkInitialized);
	if (Chunk->IsChunkInitialized())
	{
		BuildPresentation(*Chunk);
	}
}

void UOceanChunkPresentationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AOceanChunkActor* Chunk = Cast<AOceanChunkActor>(GetOwner()))
	{
		Chunk->OnChunkInitialized.RemoveDynamic(this, &ThisClass::HandleChunkInitialized);
	}

	DestroyPresentation();
	Super::EndPlay(EndPlayReason);
}

void UOceanChunkPresentationComponent::HandleChunkInitialized(AOceanChunkActor* Chunk)
{
	if (IsValid(Chunk) && Chunk == GetOwner())
	{
		BuildPresentation(*Chunk);
	}
}

void UOceanChunkPresentationComponent::BuildPresentation(AOceanChunkActor& Chunk)
{
	if (bPresentationBuilt || !Chunk.IsChunkInitialized() || !Chunk.GetRootComponent())
	{
		return;
	}

	BuildTerrain(Chunk);
	BuildWater(Chunk);
	bPresentationBuilt = IsValid(TerrainMeshComponent) || IsValid(WaterMeshComponent);

	if (bPresentationBuilt)
	{
		UE_LOG(LogOceanAdventure, Log, TEXT("Built terrain and water presentation for chunk [%d, %d]"),
			Chunk.GetChunkCoord().X, Chunk.GetChunkCoord().Y);
	}
	else
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("Failed to build presentation for chunk [%d, %d]"),
			Chunk.GetChunkCoord().X, Chunk.GetChunkCoord().Y);
	}
}

void UOceanChunkPresentationComponent::BuildTerrain(AOceanChunkActor& Chunk)
{
	UMaterialInterface* Material = TerrainMaterial.LoadSynchronous();
	if (!Material)
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("Missing terrain material: %s"), *TerrainMaterial.ToString());
		return;
	}

	TerrainMeshComponent = NewObject<UProceduralMeshComponent>(&Chunk, TEXT("OceanTerrainMesh"), RF_Transient);
	TerrainMeshComponent->SetupAttachment(Chunk.GetRootComponent());
	TerrainMeshComponent->SetMobility(EComponentMobility::Movable);
	TerrainMeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	// Ocean chunks are short-lived streaming geometry. UE 5.7's procedural-mesh ray tracing
	// proxy can still be collected by a renderer worker while a chunk is being replaced,
	// which makes FRayTracingInstanceCollector dereference a released proxy. The terrain
	// remains fully visible through the raster path and still participates in collision.
	TerrainMeshComponent->SetVisibleInRayTracing(false);
	TerrainMeshComponent->bUseAsyncCooking = true;
	Chunk.AddInstanceComponent(TerrainMeshComponent);

	const UOceanGenerationSettings* GenerationSettings = Chunk.GetGenerationSettings();
	const int32 QuadsPerSide = GenerationSettings
		? FMath::Clamp(GenerationSettings->GetTerrainResolution(), 2, 128)
		: FMath::Clamp(TerrainQuadsPerSide, 2, 128);
	const int32 VerticesPerSide = QuadsPerSide + 1;
	const float ChunkSize = Chunk.GetChunkSize();
	const float VertexSpacing = ChunkSize / static_cast<float>(QuadsPerSide);

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	Vertices.Reserve(VerticesPerSide * VerticesPerSide);
	UVs.Reserve(VerticesPerSide * VerticesPerSide);
	VertexColors.Reserve(VerticesPerSide * VerticesPerSide);
	Triangles.Reserve(QuadsPerSide * QuadsPerSide * 6);

	// Beach sits just above the waterline; rock takes over near the peaks. Derived from the
	// island height so retuning IslandPeakHeight does not silently make everything sand.
	const float EffectiveWaterHeight = GenerationSettings
		? GenerationSettings->GetWaterHeight()
		: WaterHeight;
	const float EffectiveIslandPeakHeight = GenerationSettings
		? GenerationSettings->GetIslandPeakHeight()
		: IslandPeakHeight;
	const float BeachBandHeight = GenerationSettings
		? GenerationSettings->GetBeachBandHeight()
		: 250.0f;
	const float SandLine = EffectiveWaterHeight + BeachBandHeight;
	const float StoneLine = EffectiveWaterHeight
		+ FMath::Max(500.0f, (EffectiveIslandPeakHeight - EffectiveWaterHeight) * 0.55f);

	const FVector ChunkOrigin = Chunk.GetChunkWorldOrigin();
	for (int32 X = 0; X <= QuadsPerSide; ++X)
	{
		for (int32 Y = 0; Y <= QuadsPerSide; ++Y)
		{
			const float LocalX = X * VertexSpacing;
			const float LocalY = Y * VertexSpacing;
			const float Height = GetTerrainHeight(Chunk, ChunkOrigin.X + LocalX, ChunkOrigin.Y + LocalY);
			Vertices.Emplace(LocalX, LocalY, Height);
			UVs.Emplace(X * 0.0625f, Y * 0.0625f);

			if (Height < SandLine)
			{
				VertexColors.Emplace(255, 255, 0); // Sand in WildOmission's terrain material.
			}
			else if (Height > StoneLine)
			{
				VertexColors.Emplace(255, 0, 0); // Stone.
			}
			else
			{
				VertexColors.Emplace(0, 255, 0); // Grass.
			}
		}
	}

	for (int32 X = 0; X < QuadsPerSide; ++X)
	{
		for (int32 Y = 0; Y < QuadsPerSide; ++Y)
		{
			const int32 Vertex = X * VerticesPerSide + Y;
			Triangles.Add(Vertex);
			Triangles.Add(Vertex + 1);
			Triangles.Add(Vertex + VerticesPerSide);
			Triangles.Add(Vertex + 1);
			Triangles.Add(Vertex + VerticesPerSide + 1);
			Triangles.Add(Vertex + VerticesPerSide);
		}
	}

	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices, Triangles, UVs, Normals, Tangents);
	ensureMsgf(Normals.IsEmpty() || Normals[Normals.Num() / 2].Z > 0.0f,
		TEXT("Generated ocean terrain normals must face upward."));
	TerrainMeshComponent->CreateMeshSection(
		0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, true);
	TerrainMeshComponent->SetMaterial(0, Material);
	// Register only after the complete mesh and material have been assigned. This creates a
	// single finished scene proxy instead of publishing and immediately replacing an empty one.
	TerrainMeshComponent->RegisterComponent();
}

void UOceanChunkPresentationComponent::BuildWater(AOceanChunkActor& Chunk)
{
	UStaticMesh* Mesh = WaterMesh.LoadSynchronous();
	UMaterialInterface* Material = WaterMaterial.LoadSynchronous();
	if (!Mesh || !Material)
	{
		UE_LOG(LogOceanAdventure, Error, TEXT("Missing water presentation assets: mesh=%s material=%s"),
			*WaterMesh.ToString(), *WaterMaterial.ToString());
		return;
	}

	WaterMeshComponent = NewObject<UStaticMeshComponent>(&Chunk, TEXT("OceanWaterMesh"), RF_Transient);
	WaterMeshComponent->SetupAttachment(Chunk.GetRootComponent());
	WaterMeshComponent->SetMobility(EComponentMobility::Movable);
	WaterMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterMeshComponent->SetStaticMesh(Mesh);
	WaterMeshComponent->SetMaterial(0, Material);
	Chunk.AddInstanceComponent(WaterMeshComponent);
	WaterMeshComponent->RegisterComponent();

	const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
	const FVector MeshSize = MeshBounds.BoxExtent * 2.0;
	const float ScaleX = MeshSize.X > UE_SMALL_NUMBER ? Chunk.GetChunkSize() / MeshSize.X : 1.0f;
	const float ScaleY = MeshSize.Y > UE_SMALL_NUMBER ? Chunk.GetChunkSize() / MeshSize.Y : 1.0f;
	const FVector Scale(ScaleX, ScaleY, 1.0f);
	const UOceanGenerationSettings* GenerationSettings = Chunk.GetGenerationSettings();
	const float EffectiveWaterHeight = GenerationSettings
		? GenerationSettings->GetWaterHeight()
		: WaterHeight;
	WaterMeshComponent->SetRelativeScale3D(Scale);
	WaterMeshComponent->SetRelativeLocation(
		FVector(Chunk.GetChunkSize() * 0.5f, Chunk.GetChunkSize() * 0.5f, EffectiveWaterHeight)
		- MeshBounds.Origin * Scale);
}

void UOceanChunkPresentationComponent::DestroyPresentation()
{
	if (IsValid(TerrainMeshComponent))
	{
		TerrainMeshComponent->DestroyComponent();
	}
	if (IsValid(WaterMeshComponent))
	{
		WaterMeshComponent->DestroyComponent();
	}

	TerrainMeshComponent = nullptr;
	WaterMeshComponent = nullptr;
	bPresentationBuilt = false;
}

float UOceanChunkPresentationComponent::GetTerrainHeight(
	const AOceanChunkActor& Chunk, double WorldX, double WorldY) const
{
	const FVector2D WorldPosition(static_cast<float>(WorldX), static_cast<float>(WorldY));
	if (const UOceanGenerationSettings* GenerationSettings = Chunk.GetGenerationSettings())
	{
		return GenerationSettings->SampleTerrainHeight(WorldPosition);
	}

	const float SafeNoiseScale = FMath::Max(100.0f, TerrainNoiseScale);
	const float Seed = static_cast<float>(Chunk.GetWorldSeed());
	const FVector2D SeedOffset(Seed * 0.0137f, Seed * 0.0211f);

	// Surface roughness. On its own this is just a sheet undulating about a mean -- two
	// octaves of Perlin around a base height give gentle swells, never a coastline. That is
	// why the world used to be open ocean everywhere.
	const float MacroNoise = FMath::PerlinNoise2D(WorldPosition / SafeNoiseScale + SeedOffset);
	const float DetailNoise = FMath::PerlinNoise2D(
		WorldPosition / (SafeNoiseScale * 0.28f) + SeedOffset * 1.7f);
	const float Roughness = MacroNoise * 0.78f + DetailNoise * 0.22f;

	// The island mask is what actually decides land from sea.
	const float Mask = GetIslandMask(Chunk, WorldPosition);
	const float Shape = GetIslandShapeHeight(Mask, OceanFloorHeight, WaterHeight, IslandPeakHeight);

	// The plateau, waterline, and seabed stay flat. Low-amplitude detail remains on the ramp.
	const float LandAlpha = FMath::Clamp(
		(Mask - ShorelineMask) / (1.0f - ShorelineMask), 0.0f, 1.0f);
	const float TransitionWeight = 4.0f * LandAlpha * (1.0f - LandAlpha);
	return Shape + Roughness * TerrainHeightAmplitude * BeachRoughnessScale * TransitionWeight;
}

float UOceanChunkPresentationComponent::GetIslandMask(
	const AOceanChunkActor& Chunk, const FVector2D& WorldPosition) const
{
	// The world is cut into IslandCellSize squares, each holding at most one island centre
	// jittered deterministically from the seed. Only the 3x3 cells around the sample can
	// reach it, so this stays O(9) however far out the player travels.
	const float CellSize = FMath::Max(1000.0f, IslandCellSize);
	const float NominalRadius = FMath::Max(100.0f, IslandRadius);
	const int32 Seed = Chunk.GetWorldSeed();

	const int32 BaseCellX = FMath::FloorToInt32(WorldPosition.X / CellSize);
	const int32 BaseCellY = FMath::FloorToInt32(WorldPosition.Y / CellSize);

	float BestMask = 0.0f;
	for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
	{
		for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
		{
			const int32 CellX = BaseCellX + OffsetX;
			const int32 CellY = BaseCellY + OffsetY;

			// Leaving cells empty is what makes it an archipelago instead of a regular grid.
			if (HashIslandCell(CellX, CellY, Seed, 3) > IslandDensity)
			{
				continue;
			}

			const FVector2D Centre(
				(static_cast<float>(CellX) + HashIslandCell(CellX, CellY, Seed, 0)) * CellSize,
				(static_cast<float>(CellY) + HashIslandCell(CellX, CellY, Seed, 1)) * CellSize);

			const float ShorelineRadius = NominalRadius
				* (0.6f + 0.8f * HashIslandCell(CellX, CellY, Seed, 2));
			const float OceanFloorRadius = ShorelineRadius + FMath::Max(100.0f, UnderwaterShelfWidth);
			const float ConfiguredPlateauRadius = ShorelineRadius
				* FMath::Clamp(IslandPlateauRadiusRatio, 0.0f, 0.95f);
			const float HeightAboveWater = FMath::Max(0.0f, IslandPeakHeight - WaterHeight);
			const float SafeBeachSlopeAngle = FMath::Clamp(BeachSlopeAngleDegrees, 5.0f, 40.0f);
			const float MinimumTransitionWidth = HeightAboveWater
				/ FMath::Tan(FMath::DegreesToRadians(SafeBeachSlopeAngle));
			const float PlateauRadius = FMath::Min(
				ConfiguredPlateauRadius, FMath::Max(0.0f, ShorelineRadius - MinimumTransitionWidth));

			// Warping the distance instead of the radius is what turns a circle into a
			// coastline with bays and headlands.
			// Offset per island, not per sample, so the warp stays continuous across one
			// island while two worlds with different seeds get different coastlines.
			const FVector2D ShapeOffset(
				HashIslandCell(CellX, CellY, Seed, 4) * 64.0f,
				HashIslandCell(CellX, CellY, Seed, 5) * 64.0f);
			const float ShapeNoise =
				FMath::PerlinNoise2D(WorldPosition / (ShorelineRadius * 1.5f) + ShapeOffset);
			const float RadialDistance = FVector2D::Distance(WorldPosition, Centre);
			const float WarpAlpha = FMath::Clamp(
				(RadialDistance - PlateauRadius)
					/ FMath::Max(ShorelineRadius - PlateauRadius, UE_SMALL_NUMBER),
				0.0f,
				1.0f);
			const float WarpWeight = WarpAlpha * WarpAlpha * (3.0f - 2.0f * WarpAlpha);
			const float Distance = RadialDistance
				+ ShapeNoise * ShorelineRadius * IslandEdgeDistortion * WarpWeight;

			BestMask = FMath::Max(BestMask, IslandProfileMask(
				PlateauRadius, ShorelineRadius, OceanFloorRadius, Distance));
		}
	}

	return FMath::Clamp(BestMask, 0.0f, 1.0f);
}
