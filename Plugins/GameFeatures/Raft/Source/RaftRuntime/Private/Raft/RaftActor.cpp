// Copyright Epic Games, Inc. All Rights Reserved.

#include "Raft/RaftActor.h"

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
}
