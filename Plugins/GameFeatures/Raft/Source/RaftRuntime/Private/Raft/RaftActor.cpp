// Copyright Epic Games, Inc. All Rights Reserved.

#include "Raft/RaftActor.h"

#include "Building/BuildStructureComponent.h"
#include "Building/BuildStructureVisualComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Raft/RaftBuoyancyComponent.h"
#include "Raft/RaftDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RaftActor)

ARaftActor::ARaftActor()
{
	PrimaryActorTick.bCanEverTick = false;

	DeckCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("DeckCollision"));
	SetRootComponent(DeckCollision);
	DeckCollision->SetMobility(EComponentMobility::Movable);
	DeckCollision->SetBoxExtent(FVector(124.0, 200.0, 21.0));
	DeckCollision->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	DeckCollision->SetGenerateOverlapEvents(false);
	DeckCollision->SetCanEverAffectNavigation(false);
	DeckCollision->CanCharacterStepUpOn = ECB_Yes;

	VisualPivot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualPivot"));
	VisualPivot->SetupAttachment(DeckCollision);
	VisualPivot->SetRelativeLocation(FVector(0.0, 0.0, 9.0));

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(VisualPivot);
	VisualMesh->SetMobility(EComponentMobility::Movable);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetGenerateOverlapEvents(false);
	VisualMesh->SetCanEverAffectNavigation(false);

	BuoyancyComponent = CreateDefaultSubobject<URaftBuoyancyComponent>(TEXT("BuoyancyComponent"));
	BuildStructureComponent = CreateDefaultSubobject<UBuildStructureComponent>(TEXT("BuildStructureComponent"));
	BuildStructureVisualComponent =
		CreateDefaultSubobject<UBuildStructureVisualComponent>(TEXT("BuildStructureVisualComponent"));

	bReplicates = true;
	bNetLoadOnClient = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(10.0f);
	SetNetCullDistanceSquared(FMath::Square(150000.0f));
	NetDormancy = DORM_Awake;
}

void ARaftActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyDefinition();
}

void ARaftActor::ApplyDefinition()
{
	if (!RaftDefinition)
	{
		return;
	}

	DeckCollision->SetBoxExtent(RaftDefinition->GetDeckBoxExtent());
	VisualPivot->SetRelativeLocation(RaftDefinition->GetVisualMeshOffset());
	VisualMesh->SetStaticMesh(RaftDefinition->GetVisualMesh());
	BuoyancyComponent->ApplyDefinition(RaftDefinition);
	BuildStructureComponent->SetPieceCatalog(RaftDefinition->GetBuildPieceCatalog());
}

USceneComponent* ARaftActor::GetStructureAttachRoot() const
{
	return DeckCollision.Get();
}

bool ARaftActor::CollectAnchorCells(TSet<FBuildGridCoord>& OutCells) const
{
	if (!DeckCollision)
	{
		return false;
	}

	const FVector Extent = RaftDefinition
		? RaftDefinition->GetDeckBoxExtent()
		: DeckCollision->GetUnscaledBoxExtent();
	const FBuildGridCoord MinCoord = BuildGrid::LocalToCoord(
		FVector(-Extent.X, -Extent.Y, 0.0),
		BuildGridSettings);
	const FBuildGridCoord MaxCoord = BuildGrid::LocalToCoord(
		FVector(
			Extent.X - KINDA_SMALL_NUMBER,
			Extent.Y - KINDA_SMALL_NUMBER,
			0.0),
		BuildGridSettings);

	for (int32 X = MinCoord.X; X <= MaxCoord.X; ++X)
	{
		for (int32 Y = MinCoord.Y; Y <= MaxCoord.Y; ++Y)
		{
			OutCells.Add(FBuildGridCoord(X, Y, 0));
		}
	}
	return true;
}

bool ARaftActor::IsCellAnchored(const FBuildGridCoord& Coord) const
{
	if (Coord.Level != 0)
	{
		return false;
	}

	TSet<FBuildGridCoord> AnchorCells;
	CollectAnchorCells(AnchorCells);
	return AnchorCells.Contains(Coord);
}

void ARaftActor::OnStructureBoundsChanged(const FBox& LocalBounds)
{
	if (!LocalBounds.IsValid || !DeckCollision)
	{
		return;
	}

	const FVector ExistingExtent = DeckCollision->GetUnscaledBoxExtent();
	const FVector StructureExtent = LocalBounds.GetExtent();
	DeckCollision->SetBoxExtent(
		FVector(StructureExtent.X, StructureExtent.Y, ExistingExtent.Z),
		true);

	if (HasAuthority() && BuoyancyComponent)
	{
		BuoyancyComponent->RebuildFromStructure(LocalBounds);
	}
}
