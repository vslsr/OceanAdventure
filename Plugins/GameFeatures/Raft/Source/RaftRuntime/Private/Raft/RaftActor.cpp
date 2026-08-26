// Copyright Epic Games, Inc. All Rights Reserved.

#include "Raft/RaftActor.h"

#include "Building/BuildStructureComponent.h"
#include "Building/BuildStructureVisualComponent.h"
#include "Components/BoxComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Raft/RaftBuoyancyComponent.h"
#include "Raft/RaftDefinition.h"
#include "RaftRuntimeModule.h"

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

void ARaftActor::PreInitializeComponents()
{
	Super::PreInitializeComponents();
	UGameFrameworkComponentManager::AddGameFrameworkComponentReceiver(this);
}

void ARaftActor::BeginPlay()
{
	Super::BeginPlay();
	UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(
		this, UGameFrameworkComponentManager::NAME_GameActorReady);
}

void ARaftActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UGameFrameworkComponentManager::RemoveGameFrameworkComponentReceiver(this);
	Super::EndPlay(EndPlayReason);
}

void ARaftActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyDefinition();
}

void ARaftActor::PostInitializeComponents()
{
	// The build component caches grid settings in its BeginPlay, which runs after this.
	RecomputeGridAlignment();

	// A raft whose root came back Static silently swallows every buoyancy and steering write:
	// SetActorLocationAndRotation is discarded, and the only trace is a per-frame PIE warning.
	// The constructor already asks for Movable, but a Mobility saved on the Blueprint or on a
	// placed instance overrides that and no C++ change can reach it, so it is corrected here.
	if (DeckCollision && DeckCollision->Mobility != EComponentMobility::Movable)
	{
		UE_LOG(
			LogRaft,
			Warning,
			TEXT("[Raft] %s : DeckCollision was not Movable, so the hull could not be moved at "
				 "all. Forcing Movable -- clear the stale Mobility on the Blueprint or the "
				 "placed instance so this stops being needed."),
			*GetPathName());
		DeckCollision->SetMobility(EComponentMobility::Movable);
	}

	Super::PostInitializeComponents();
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
	RecomputeGridAlignment();
}

FVector ARaftActor::GetBaseDeckExtent() const
{
	// Always the authored deck, never DeckCollision's current extent: that one grows with
	// the built structure, and feeding it back in would make the anchor area self-expand.
	return RaftDefinition
		? RaftDefinition->GetDeckBoxExtent()
		: FVector(124.0, 200.0, 21.0);
}

void ARaftActor::RecomputeGridAlignment()
{
	const FVector Extent = GetBaseDeckExtent();
	// One hull/deck module is exactly one physical raft footprint. The grid coordinate is only
	// the replicated occupancy address; it must never introduce a separately authored snap
	// distance that can leave adjacent hull meshes overlapping or separated by a gap.
	BuildGridSettings.CellSizeXY = FVector2D(Extent.X * 2.0, Extent.Y * 2.0);
	const FVector2D CellSize = BuildGrid::GetCellSize(BuildGridSettings);

	// Only whole cells that fit inside the deck may be anchored; a deck that is not an exact
	// multiple of CellSize keeps its remainder as an un-buildable visual margin rather than
	// letting pieces overhang the water.
	const int32 CountX = FMath::FloorToInt((Extent.X * 2.0) / CellSize.X);
	const int32 CountY = FMath::FloorToInt((Extent.Y * 2.0) / CellSize.Y);

	// Odd cell counts need a half-cell shift to stay centred on the raft origin.
	BuildGridSettings.CellOrigin = FVector2D(
		(CountX % 2 == 0) ? 0.0 : -CellSize.X * 0.5,
		(CountY % 2 == 0) ? 0.0 : -CellSize.Y * 0.5);

	// Level 0 is the deck's top face, so pieces only need their own half-height as offset.
	BuildGridSettings.BaseHeight = Extent.Z;

	if (CountX <= 0 || CountY <= 0)
	{
		AnchorMin = FIntPoint(0, 0);
		AnchorMax = FIntPoint(-1, -1);
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[Raft] Deck %s is smaller than one %s build cell; building is disabled."),
			*Extent.ToString(),
			*CellSize.ToString());
		return;
	}

	AnchorMin = FIntPoint(-(CountX / 2), -(CountY / 2));
	AnchorMax = FIntPoint(AnchorMin.X + CountX - 1, AnchorMin.Y + CountY - 1);
}

USceneComponent* ARaftActor::GetStructureAttachRoot() const
{
	return DeckCollision.Get();
}

bool ARaftActor::CollectAnchorCells(TSet<FBuildGridCoord>& OutCells) const
{
	if (AnchorMax.X < AnchorMin.X || AnchorMax.Y < AnchorMin.Y)
	{
		return false;
	}

	for (int32 X = AnchorMin.X; X <= AnchorMax.X; ++X)
	{
		for (int32 Y = AnchorMin.Y; Y <= AnchorMax.Y; ++Y)
		{
			OutCells.Add(FBuildGridCoord(X, Y, 0));
		}
	}
	return true;
}

bool ARaftActor::IsCellAnchored(const FBuildGridCoord& Coord) const
{
	// O(1): placement validation calls this several times per frame.
	return Coord.Level == 0
		&& Coord.X >= AnchorMin.X && Coord.X <= AnchorMax.X
		&& Coord.Y >= AnchorMin.Y && Coord.Y <= AnchorMax.Y;
}

void ARaftActor::OnStructureBoundsChanged(const FBox& LocalPieceBounds)
{
	if (!DeckCollision)
	{
		return;
	}

	// The base deck is the floor: an empty structure must not shrink or grow it.
	const FVector BaseExtent = GetBaseDeckExtent();
	FVector DeckExtent = BaseExtent;

	if (LocalPieceBounds.IsValid)
	{
		// DeckCollision is the root component and the characters' movement base, so it stays
		// centred on the actor origin; asymmetric builds are covered by a symmetric envelope
		// instead of moving the root (which would teleport the whole raft).
		DeckExtent.X = FMath::Max3(
			BaseExtent.X,
			FMath::Abs(LocalPieceBounds.Min.X),
			FMath::Abs(LocalPieceBounds.Max.X));
		DeckExtent.Y = FMath::Max3(
			BaseExtent.Y,
			FMath::Abs(LocalPieceBounds.Min.Y),
			FMath::Abs(LocalPieceBounds.Max.Y));
	}

	const FVector NewExtent(DeckExtent.X, DeckExtent.Y, BaseExtent.Z);
	if (!NewExtent.Equals(DeckCollision->GetUnscaledBoxExtent(), 0.1))
	{
		// Only touch the movement base when the size actually changed: every update re-grounds
		// the characters standing on the deck.
		DeckCollision->SetBoxExtent(NewExtent, true);
	}

	if (HasAuthority() && BuoyancyComponent)
	{
		BuoyancyComponent->RebuildFromStructure(
			FBox::BuildAABB(FVector(0.0, 0.0, 0.0), NewExtent));
	}
}
