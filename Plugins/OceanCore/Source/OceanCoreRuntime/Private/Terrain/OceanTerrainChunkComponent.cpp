// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/OceanTerrainChunkComponent.h"

#include "Async/Async.h"
#include "DrawDebugHelpers.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "OceanCoreRuntimeModule.h"
#include "Terrain/OceanTerrainSubsystem.h"
#include "World/OceanChunkActor.h"

using namespace UE::Geometry;

namespace
{
	/** Lifts the debug ink off the surface it traces so it is not lost to depth fighting. */
	constexpr float DebugInkLift = 2.0f;
}

UOceanTerrainChunkComponent::UOceanTerrainChunkComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// Terrain is opaque, static-shaped, and never overlaps: it wants to block, and it has no
	// business generating overlap events for every actor standing on it.
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetCollisionObjectType(ECC_WorldStatic);
	SetCollisionResponseToAllChannels(ECR_Block);
	SetGenerateOverlapEvents(false);
	bUseAsyncCooking = true;
}

void UOceanTerrainChunkComponent::BeginPlay()
{
	Super::BeginPlay();
	BindToOwningChunk();
}

void UOceanTerrainChunkComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The task captures nothing that outlives this component, but it does write into a result
	// the game thread reads, so it has to be finished before the component goes away.
	if (BuildTask.IsValid())
	{
		BuildTask.Wait();
	}

	if (AOceanChunkActor* Chunk = Cast<AOceanChunkActor>(GetOwner()))
	{
		Chunk->OnChunkInitialized.RemoveDynamic(this, &UOceanTerrainChunkComponent::HandleChunkInitialized);
	}

	if (UOceanTerrainSubsystem* Terrain = UOceanTerrainSubsystem::Get(this))
	{
		Terrain->UnregisterChunk(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UOceanTerrainChunkComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bDrawInk)
	{
		DrawInk();
	}
}

void UOceanTerrainChunkComponent::BindToOwningChunk()
{
	AOceanChunkActor* Chunk = Cast<AOceanChunkActor>(GetOwner());
	if (!Chunk)
	{
		// Standalone use (a test map, the editor preview) is fine; it just means whoever placed
		// the component drives RebuildTerrain itself.
		return;
	}

	Chunk->OnChunkInitialized.AddUniqueDynamic(this, &UOceanTerrainChunkComponent::HandleChunkInitialized);

	// The chunk may have initialised before this component began play -- on a client the
	// replicated state can arrive first -- in which case the delegate has already fired and
	// waiting for it would leave this chunk permanently blank.
	if (Chunk->IsChunkInitialized())
	{
		HandleChunkInitialized(Chunk);
	}
}

void UOceanTerrainChunkComponent::HandleChunkInitialized(AOceanChunkActor* Chunk)
{
	if (!Chunk)
	{
		return;
	}

	// The chunk grid and the cell grid have to agree, or every chunk renders at the wrong
	// offset while each individual cell still looks correct -- which is a miserable thing to
	// debug from the picture alone.
	const float ExpectedChunkSize = OceanTerrain::ChunkSize;
	if (!FMath::IsNearlyEqual(Chunk->GetChunkSize(), ExpectedChunkSize))
	{
		UE_LOG(
			LogOceanCore,
			Error,
			TEXT("Chunk %s has ChunkSize %.1f but the terrain grid is %d cells of %.1f = %.1f. ")
			TEXT("Set the world manager's ChunkSize to match OceanTerrain::ChunkGrid, or the ")
			TEXT("terrain will not line up with the chunks that stream it."),
			*Chunk->GetChunkCoord().ToString(),
			Chunk->GetChunkSize(),
			OceanTerrain::ChunkGrid,
			OceanTerrain::CellSize,
			ExpectedChunkSize);
	}

	CachedWorldSeed = Chunk->GetWorldSeed();
	CachedChunkCoord = Chunk->GetChunkCoord();
	bHasChunkIdentity = true;

	if (UOceanTerrainSubsystem* Terrain = UOceanTerrainSubsystem::Get(this))
	{
		Terrain->EnsureStore(static_cast<uint32>(CachedWorldSeed));
		Terrain->RegisterChunk(this, CachedChunkCoord);
	}

	RequestRebuild();
}

void UOceanTerrainChunkComponent::RequestRebuild()
{
	if (!bHasChunkIdentity)
	{
		// Nothing to rebuild yet: the owning chunk has not initialised, so this component does
		// not know which part of the world it is.
		return;
	}

	TArray<int32> Overrides;
	if (UOceanTerrainSubsystem* Terrain = UOceanTerrainSubsystem::Get(this))
	{
		Terrain->CollectWindowOverrides(CachedChunkCoord, Overrides);
	}
	RebuildTerrain(CachedWorldSeed, CachedChunkCoord, Overrides);
}

void UOceanTerrainChunkComponent::RebuildTerrain(
	int32 InWorldSeed,
	FIntPoint InChunkCoord,
	const TArray<int32>& InOverrides)
{
	const uint32 WorldSeed = static_cast<uint32>(InWorldSeed);
	const OceanTerrain::FTerrainPalette Palette{ GroundColor };
	const double LocalSeaLevel = SeaLevel;
	const int32 Serial = ++BuildSerial;

	// The task takes copies of everything it needs. That is the whole reason the builder's
	// input is a seed plus a flat override array instead of a callback into a patch store:
	// plain data crosses a thread boundary and a callback does not.
	TArray<int32> Overrides = InOverrides;

	TWeakObjectPtr<UOceanTerrainChunkComponent> WeakThis(this);
	BuildTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[WeakThis, WorldSeed, InChunkCoord, Palette, LocalSeaLevel, Serial, Overrides = MoveTemp(Overrides)]()
		{
			TArray<int32> Codes;
			OceanTerrain::FTerrainMeshData Mesh;
			OceanTerrain::FTerrainInkData Ink;
			OceanTerrain::BuildChunkCodes(WorldSeed, InChunkCoord, Overrides, Codes);
			OceanTerrain::BuildChunkMesh(InChunkCoord, Codes, Palette, LocalSeaLevel, Mesh);
			OceanTerrain::BuildChunkInk(InChunkCoord, Codes, Ink);

			AsyncTask(
				ENamedThreads::GameThread,
				[WeakThis, Serial, Mesh = MoveTemp(Mesh), Ink = MoveTemp(Ink)]() mutable
				{
					UOceanTerrainChunkComponent* Component = WeakThis.Get();
					if (!Component || Component->BuildSerial != Serial)
					{
						// A newer request has already been made; applying this one would put an
						// older terrain on screen and then never correct it.
						return;
					}
					Component->ApplyBuild(MoveTemp(Mesh), MoveTemp(Ink));
				});
		});
}

void UOceanTerrainChunkComponent::ApplyBuild(
	OceanTerrain::FTerrainMeshData&& Mesh,
	OceanTerrain::FTerrainInkData&& Ink)
{
	check(IsInGameThread());

	FDynamicMesh3 NewMesh;
	NewMesh.EnableAttributes();
	NewMesh.Attributes()->EnablePrimaryColors();

	FDynamicMeshNormalOverlay* Normals = NewMesh.Attributes()->PrimaryNormals();
	FDynamicMeshColorOverlay* Colors = NewMesh.Attributes()->PrimaryColors();

	const int32 VertexCount = Mesh.Positions.Num();
	TArray<int32> VertexIds;
	TArray<int32> NormalIds;
	TArray<int32> ColorIds;
	VertexIds.Reserve(VertexCount);
	NormalIds.Reserve(VertexCount);
	ColorIds.Reserve(VertexCount);
	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		VertexIds.Add(NewMesh.AppendVertex(FVector3d(Mesh.Positions[Index])));
		NormalIds.Add(Normals->AppendElement(Mesh.Normals[Index]));
		const FLinearColor Linear = FLinearColor::FromSRGBColor(Mesh.Colors[Index]);
		ColorIds.Add(Colors->AppendElement(FVector4f(Linear.R, Linear.G, Linear.B, Linear.A)));
	}

	int32 SplitVertices = 0;
	for (int32 Triangle = 0; Triangle + 2 < Mesh.Indices.Num(); Triangle += 3)
	{
		const int32 A = static_cast<int32>(Mesh.Indices[Triangle]);
		const int32 B = static_cast<int32>(Mesh.Indices[Triangle + 1]);
		const int32 C = static_cast<int32>(Mesh.Indices[Triangle + 2]);

		int32 TriangleId = NewMesh.AppendTriangle(FIndex3i(VertexIds[A], VertexIds[B], VertexIds[C]));
		if (TriangleId < 0)
		{
			// FDynamicMesh3 refuses a triangle that would make an edge non-manifold, and stepped
			// terrain produces those wherever two cliffs meet along one edge. Duplicating the
			// vertices is the accepted fix; the alternative is a silently missing face, which
			// reads as a hole in the ground you can fall through.
			const int32 DuplicateA = NewMesh.AppendVertex(FVector3d(Mesh.Positions[A]));
			const int32 DuplicateB = NewMesh.AppendVertex(FVector3d(Mesh.Positions[B]));
			const int32 DuplicateC = NewMesh.AppendVertex(FVector3d(Mesh.Positions[C]));
			TriangleId = NewMesh.AppendTriangle(FIndex3i(DuplicateA, DuplicateB, DuplicateC));
			SplitVertices += 3;
		}
		if (TriangleId < 0)
		{
			continue;
		}

		Normals->SetTriangle(TriangleId, FIndex3i(NormalIds[A], NormalIds[B], NormalIds[C]));
		Colors->SetTriangle(TriangleId, FIndex3i(ColorIds[A], ColorIds[B], ColorIds[C]));
	}

	if (SplitVertices > 0)
	{
		UE_LOG(
			LogOceanCore,
			Verbose,
			TEXT("Terrain chunk split %d vertices to keep the mesh manifold."),
			SplitVertices);
	}

	if (FillMaterial)
	{
		SetMaterial(0, FillMaterial);
	}

	SetMesh(MoveTemp(NewMesh));
	NotifyMeshUpdated();

	// The render mesh is the collision mesh. Complex-as-simple keeps them one surface instead
	// of letting a convex hull approximate the steps into a ramp.
	EnableComplexAsSimpleCollision();
	UpdateCollision(false);

	InkData = MoveTemp(Ink);
	bTerrainBuilt = true;
	SetComponentTickEnabled(bDrawInk);
}

void UOceanTerrainChunkComponent::DrawInk() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FTransform& Transform = GetComponentTransform();
	const FVector Lift(0.0, 0.0, DebugInkLift);
	for (const OceanTerrain::FTerrainInkSegment& Segment : InkData.Segments)
	{
		const FColor Color =
			(Segment.Kind == OceanTerrain::EEdgeInk::Cliff ? DebugCliffColor : DebugFoldColor)
				.ToFColor(true);
		DrawDebugLine(
			World,
			Transform.TransformPosition(FVector(Segment.Start)) + Lift,
			Transform.TransformPosition(FVector(Segment.End)) + Lift,
			Color,
			false,
			-1.0f,
			0,
			2.0f);
	}
}
