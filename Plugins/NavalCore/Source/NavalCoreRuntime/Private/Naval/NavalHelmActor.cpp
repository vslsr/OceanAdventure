// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalHelmActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Naval/NavalCoreTypes.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalHelmComponent.h"
#include "Naval/NavalPartComponent.h"
#include "Naval/NavalRegistrySubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalHelmActor)

ANavalHelmActor::ANavalHelmActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	// The console rides a moving deck; its own transform never changes relative to the raft.
	SetReplicateMovement(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ConsoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ConsoleMesh"));
	ConsoleMesh->SetupAttachment(SceneRoot);
	ConsoleMesh->SetRelativeLocation(FVector(0.0, 0.0, 60.0));
	// No collision at all: a wheel that could be shot from open water would turn a single
	// long-range hit into a paralysed ship, which the design explicitly rules out.
	ConsoleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ConsoleMesh->SetGenerateOverlapEvents(false);
	ConsoleMesh->SetCanEverAffectNavigation(false);

	CoreSeatCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("CoreSeatCollision"));
	CoreSeatCollision->SetupAttachment(SceneRoot);
	CoreSeatCollision->SetBoxExtent(FVector(70.0, 70.0, 35.0));
	CoreSeatCollision->SetRelativeLocation(FVector(0.0, 0.0, 20.0));
	CoreSeatCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CoreSeatCollision->SetCollisionObjectType(ECC_WorldDynamic);
	CoreSeatCollision->SetCollisionResponseToAllChannels(ECR_Block);
	CoreSeatCollision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CoreSeatCollision->SetCanEverAffectNavigation(false);

	CorePart = CreateDefaultSubobject<UNavalPartComponent>(TEXT("CorePart"));
	// Durability is set here rather than in the component default so the "2.5-3x a wall"
	// relationship from design 8.3.1 is visible next to the thing it describes.
	CorePart->ConfigureFromFragment(
		ENavalPartType::HelmCore, /*MaxDurability=*/750.0f, /*DeploySeconds=*/0.0f, /*ConstructionSeconds=*/0.0f);
}

void ANavalHelmActor::BeginPlay()
{
	Super::BeginPlay();

	// Registered here rather than by the helm component: the component spawns this actor and
	// then attaches it, so at that point GetHelmComponent() cannot yet walk back to its owner.
	// Registration only needs the actor itself; the queries that need the component run later.
	if (UNavalRegistrySubsystem* Registry = UNavalRegistrySubsystem::Get(this))
	{
		Registry->RegisterStation(this);
	}
}

void ANavalHelmActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNavalRegistrySubsystem* Registry = UNavalRegistrySubsystem::Get(this))
	{
		Registry->UnregisterStation(this);
	}

	Super::EndPlay(EndPlayReason);
}

FVector ANavalHelmActor::GetStationWorldLocation() const
{
	const UNavalHelmComponent* Helm = GetHelmComponent();
	// The console mesh is offset up the root; the helm component's own answer is the one
	// CanOccupy measures against, so anything else would prompt at a different place than
	// the server accepts at.
	return Helm ? Helm->GetHelmWorldLocation() : GetActorLocation();
}

double ANavalHelmActor::GetStationInteractionRange() const
{
	const UNavalHelmComponent* Helm = GetHelmComponent();
	return Helm ? static_cast<double>(Helm->GetInteractionRange()) : 0.0;
}

bool ANavalHelmActor::CanOperateStation(const AActor* Candidate, FGameplayTag& OutFailReason) const
{
	const UNavalHelmComponent* Helm = GetHelmComponent();
	if (!Helm)
	{
		// A console whose vessel lost its helm component is scenery, not a station.
		OutFailReason = NavalGameplayTags::Fail_NotOperational;
		return false;
	}
	return Helm->CanOccupy(Candidate, OutFailReason);
}

UNavalHelmComponent* ANavalHelmActor::GetHelmComponent() const
{
	const AActor* Vessel = GetAttachParentActor();
	if (!Vessel)
	{
		Vessel = GetOwner();
	}
	return Vessel ? Vessel->FindComponentByClass<UNavalHelmComponent>() : nullptr;
}
