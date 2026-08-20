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

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanChunkPresentationComponent)

namespace
{
	const TCHAR* TerrainMaterialPath =
		TEXT("/OceanAdventure/Environment/WildOmission/WorldGeneration/M_Terrain.M_Terrain");
	const TCHAR* WaterMeshPath =
		TEXT("/OceanAdventure/Environment/WildOmission/WorldGeneration/SM_Water.SM_Water");
	const TCHAR* WaterMaterialPath =
		TEXT("/OceanAdventure/Environment/WildOmission/WildOmissionCore/Art/Environment/M_Water.M_Water");
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
	TerrainMeshComponent->bUseAsyncCooking = true;
	Chunk.AddInstanceComponent(TerrainMeshComponent);
	TerrainMeshComponent->RegisterComponent();

	const int32 QuadsPerSide = FMath::Clamp(TerrainQuadsPerSide, 2, 128);
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

			if (Height < WaterHeight + 80.0f)
			{
				VertexColors.Emplace(255, 255, 0); // Sand in WildOmission's terrain material.
			}
			else if (Height > WaterHeight + TerrainHeightAmplitude * 0.7f)
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
			Triangles.Add(Vertex + VerticesPerSide);
			Triangles.Add(Vertex + 1);
			Triangles.Add(Vertex + 1);
			Triangles.Add(Vertex + VerticesPerSide);
			Triangles.Add(Vertex + VerticesPerSide + 1);
		}
	}

	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices, Triangles, UVs, Normals, Tangents);
	TerrainMeshComponent->CreateMeshSection(
		0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, true);
	TerrainMeshComponent->SetMaterial(0, Material);
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
	WaterMeshComponent->SetRelativeScale3D(Scale);
	WaterMeshComponent->SetRelativeLocation(
		FVector(Chunk.GetChunkSize() * 0.5f, Chunk.GetChunkSize() * 0.5f, WaterHeight)
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
	const float SafeNoiseScale = FMath::Max(100.0f, TerrainNoiseScale);
	const float Seed = static_cast<float>(Chunk.GetWorldSeed());
	const FVector2D SeedOffset(Seed * 0.0137f, Seed * 0.0211f);
	const FVector2D WorldPosition(static_cast<float>(WorldX), static_cast<float>(WorldY));
	const float MacroNoise = FMath::PerlinNoise2D(WorldPosition / SafeNoiseScale + SeedOffset);
	const float DetailNoise = FMath::PerlinNoise2D(
		WorldPosition / (SafeNoiseScale * 0.28f) + SeedOffset * 1.7f);
	return TerrainBaseHeight
		+ (MacroNoise * 0.78f + DetailNoise * 0.22f) * TerrainHeightAmplitude;
}
