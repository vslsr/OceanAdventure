// Copyright Epic Games, Inc. All Rights Reserved.

#include "Raft/RaftVesselActor.h"

#include "Components/BoxComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalHelmComponent.h"
#include "Raft/RaftBuoyancyComponent.h"
#include "Raft/RaftDefinition.h"
#include "RaftRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RaftVesselActor)

namespace RaftVesselDefaults
{
	const FVector DeckExtent(100.0, 100.0, 75.0);
	const FVector HelmOperatorLocalOffset(-35.0, 0.0, 163.0);
}

ARaftVesselActor::ARaftVesselActor()
{
	PrimaryActorTick.bCanEverTick = false;

	DeckCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("DeckCollision"));
	SetRootComponent(DeckCollision);
	DeckCollision->SetMobility(EComponentMobility::Movable);
	DeckCollision->SetBoxExtent(RaftVesselDefaults::DeckExtent);
	DeckCollision->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	DeckCollision->SetGenerateOverlapEvents(false);
	DeckCollision->SetCanEverAffectNavigation(false);
	DeckCollision->CanCharacterStepUpOn = ECB_Yes;

	VisualPivot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualPivot"));
	VisualPivot->SetupAttachment(DeckCollision);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(VisualPivot);
	VisualMesh->SetMobility(EComponentMobility::Movable);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetGenerateOverlapEvents(false);
	VisualMesh->SetCanEverAffectNavigation(false);

	HelmOperatorPoint = CreateDefaultSubobject<USceneComponent>(TEXT("HelmOperatorPoint"));
	HelmOperatorPoint->SetupAttachment(DeckCollision);
	HelmOperatorPoint->SetRelativeLocation(RaftVesselDefaults::HelmOperatorLocalOffset);
	HelmOperatorPoint->ComponentTags.AddUnique(NavalHelmStation::GetOperatorPointComponentTag());

	BuoyancyComponent = CreateDefaultSubobject<URaftBuoyancyComponent>(TEXT("BuoyancyComponent"));

	bReplicates = true;
	bNetLoadOnClient = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(10.0f);
	SetNetCullDistanceSquared(FMath::Square(150000.0f));
	NetDormancy = DORM_Awake;
}

void ARaftVesselActor::PreInitializeComponents()
{
	Super::PreInitializeComponents();
	UGameFrameworkComponentManager::AddGameFrameworkComponentReceiver(this);
}

void ARaftVesselActor::BeginPlay()
{
	Super::BeginPlay();
	UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(
		this, UGameFrameworkComponentManager::NAME_GameActorReady);
}

void ARaftVesselActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UGameFrameworkComponentManager::RemoveGameFrameworkComponentReceiver(this);
	Super::EndPlay(EndPlayReason);
}

void ARaftVesselActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyDefinition();
}

void ARaftVesselActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Saved Blueprint/level overrides can outlive the constructor default. A static root makes
	// both buoyancy and steering writes fail, so every vessel repairs the invariant here.
	if (DeckCollision && DeckCollision->Mobility != EComponentMobility::Movable)
	{
		UE_LOG(
			LogRaft,
			Warning,
			TEXT("[Raft] %s : DeckCollision was not Movable. Forcing Movable; clear the stale "
				 "Blueprint or placed-instance override."),
			*GetPathName());
		DeckCollision->SetMobility(EComponentMobility::Movable);
	}
}

void ARaftVesselActor::ApplyDefinition()
{
	if (!RaftDefinition)
	{
		return;
	}

	DeckCollision->SetBoxExtent(RaftDefinition->GetDeckBoxExtent());
	VisualPivot->SetRelativeLocation(RaftDefinition->GetVisualMeshOffset());
	VisualMesh->SetStaticMesh(RaftDefinition->GetVisualMesh());
	HelmOperatorPoint->SetRelativeLocation(RaftDefinition->GetDirectHelmOperatorLocalOffset());
	HelmOperatorPoint->SetRelativeRotation(
		FRotator(0.0f, RaftDefinition->GetDirectHelmOperatorLocalYaw(), 0.0f));
	BuoyancyComponent->ApplyDefinition(RaftDefinition);
}

FVector ARaftVesselActor::GetBaseDeckExtent() const
{
	return RaftDefinition ? RaftDefinition->GetDeckBoxExtent() : RaftVesselDefaults::DeckExtent;
}

UNavalHelmComponent* ARaftVesselActor::GetHelmComponent() const
{
	return FindComponentByClass<UNavalHelmComponent>();
}

bool ARaftVesselActor::CanOperate(const AActor* Candidate, FGameplayTag& OutFailReason) const
{
	if (!Candidate)
	{
		OutFailReason = NavalGameplayTags::Fail_WrongTeam;
		return false;
	}
	if (!RaftDefinition || !RaftDefinition->AllowsDirectHelmInteraction())
	{
		OutFailReason = NavalGameplayTags::Fail_NotOperational;
		return false;
	}

	const UNavalHelmComponent* Helm = GetHelmComponent();
	if (!Helm)
	{
		OutFailReason = NavalGameplayTags::Fail_NotOperational;
		return false;
	}

	if (!IsWithinInteractionRange(Candidate))
	{
		OutFailReason = NavalGameplayTags::Fail_TooFar;
		return false;
	}

	return Helm->CanOccupy(Candidate, OutFailReason);
}

bool ARaftVesselActor::TryOccupy(AActor* NewOperator)
{
	if (!HasAuthority())
	{
		return false;
	}

	FGameplayTag FailReason;
	if (!CanOperate(NewOperator, FailReason))
	{
		return false;
	}

	UNavalHelmComponent* Helm = GetHelmComponent();
	return Helm && Helm->TryOccupyFromStation(NewOperator, this);
}

void ARaftVesselActor::ReleaseOperator(AActor* LeavingOperator)
{
	if (HasAuthority())
	{
		if (UNavalHelmComponent* Helm = GetHelmComponent())
		{
			Helm->ReleaseHelm(LeavingOperator);
		}
	}
}

FTransform ARaftVesselActor::GetOperatorTransform() const
{
	return HelmOperatorPoint ? HelmOperatorPoint->GetComponentTransform() : GetActorTransform();
}

FVector ARaftVesselActor::GetInteractionLocation() const
{
	return GetOperatorTransform().GetLocation();
}

bool ARaftVesselActor::IsWithinInteractionRange(const AActor* Candidate) const
{
	return Candidate && RaftDefinition && RaftDefinition->AllowsDirectHelmInteraction()
		&& FVector::DistSquared(Candidate->GetActorLocation(), GetInteractionLocation())
			<= FMath::Square(static_cast<double>(RaftDefinition->GetDirectHelmInteractionRange()));
}
