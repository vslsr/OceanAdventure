// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalHelmActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Naval/NavalCoreTypes.h"
#include "Naval/NavalHelmComponent.h"
#include "Naval/NavalPartComponent.h"

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

UNavalHelmComponent* ANavalHelmActor::GetHelmComponent() const
{
	const AActor* Vessel = GetAttachParentActor();
	if (!Vessel)
	{
		Vessel = GetOwner();
	}
	return Vessel ? Vessel->FindComponentByClass<UNavalHelmComponent>() : nullptr;
}
