// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalHelmActor.h"

#include "Components/BoxComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Naval/NavalCoreTypes.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalHelmComponent.h"
#include "Naval/NavalPartComponent.h"
#include "NavalCoreRuntimeModule.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalHelmActor)

ANavalHelmActor::ANavalHelmActor()
{
	// Only the wheel needs the tick, and only where there is someone to see it turn:
	// BeginPlay switches it off on a dedicated server.
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// The console rides a moving deck; its own transform never changes relative to the raft.
	SetReplicateMovement(false);
	// Spawned at BeginPlay and then silent for minutes at a time; dormancy would delay the
	// first replication of the console to a client that arrives later.
	NetDormancy = DORM_Never;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// Greybox shapes so a vessel has a console a player can see and walk up to before any art
	// exists. A helm Blueprint replaces both meshes without touching the layout below.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	ConsoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ConsoleMesh"));
	ConsoleMesh->SetupAttachment(SceneRoot);
	// A 60x60x90 pedestal standing on the deck rather than floating over it.
	ConsoleMesh->SetRelativeLocation(FVector(0.0, 0.0, 45.0));
	ConsoleMesh->SetRelativeScale3D(FVector(0.6, 0.6, 0.9));
	if (CubeMeshFinder.Succeeded())
	{
		ConsoleMesh->SetStaticMesh(CubeMeshFinder.Object);
	}
	// No collision at all: a wheel that could be shot from open water would turn a single
	// long-range hit into a paralysed ship, which the design explicitly rules out.
	ConsoleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ConsoleMesh->SetGenerateOverlapEvents(false);
	ConsoleMesh->SetCanEverAffectNavigation(false);

	WheelPivot = CreateDefaultSubobject<USceneComponent>(TEXT("WheelPivot"));
	WheelPivot->SetupAttachment(SceneRoot);
	// Head height, tipped over the front of the pedestal the way a ship's wheel sits.
	WheelPivot->SetRelativeLocation(FVector(18.0, 0.0, 110.0));

	WheelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WheelMesh"));
	WheelMesh->SetupAttachment(WheelPivot);
	// The basic cylinder stands on its Z axis; pitching it flat leaves a 90cm disc whose
	// spin axis is the hull's forward axis, which is what WheelPivot's roll then turns.
	WheelMesh->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	WheelMesh->SetRelativeScale3D(FVector(0.9, 0.9, 0.08));
	if (CylinderMeshFinder.Succeeded())
	{
		WheelMesh->SetStaticMesh(CylinderMeshFinder.Object);
	}
	WheelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WheelMesh->SetGenerateOverlapEvents(false);
	WheelMesh->SetCanEverAffectNavigation(false);

	OperatorPoint = CreateDefaultSubobject<USceneComponent>(TEXT("OperatorPoint"));
	OperatorPoint->SetupAttachment(SceneRoot);
	// Behind the console, facing the bow: the character stands at the wheel and looks the way
	// the ship is going, so the steering keys read as ship-relative without any camera work.
	OperatorPoint->SetRelativeLocation(FVector(-75.0, 0.0, 88.0));

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

void ANavalHelmActor::PreInitializeComponents()
{
	Super::PreInitializeComponents();
	UGameFrameworkComponentManager::AddGameFrameworkComponentReceiver(this);
}

void ANavalHelmActor::BeginPlay()
{
	Super::BeginPlay();

	if (IsNetMode(NM_DedicatedServer))
	{
		SetActorTickEnabled(false);
	}
}

void ANavalHelmActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!WheelPivot)
	{
		return;
	}

	// Presentation only. The steering that actually moves the hull is the server's copy on
	// the helm component; this just follows it so the wheel is never the thing being trusted.
	const UNavalHelmComponent* Helm = GetHelmComponent();
	const float TargetAngle = Helm ? Helm->GetSteerIntent() * WheelFullSteerDegrees : 0.0f;
	WheelAngleDegrees = FMath::FInterpTo(WheelAngleDegrees, TargetAngle, DeltaTime, WheelInterpSpeed);
	WheelPivot->SetRelativeRotation(FRotator(0.0f, 0.0f, WheelAngleDegrees));
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

AActor* ANavalHelmActor::GetHelmOperator() const
{
	const UNavalHelmComponent* Helm = GetHelmComponent();
	return Helm ? Helm->GetOperator() : nullptr;
}

bool ANavalHelmActor::CanOperate(const AActor* Candidate, FGameplayTag& OutFailReason) const
{
	if (!Candidate)
	{
		OutFailReason = NavalGameplayTags::Fail_WrongTeam;
		return false;
	}

	const UNavalHelmComponent* Helm = GetHelmComponent();
	if (!Helm)
	{
		// Not on a vessel: either scenery, or a wheel someone is carrying or has set down on
		// dry land. Nothing to steer from here.
		OutFailReason = NavalGameplayTags::Fail_NotOperational;
		return false;
	}

	// A wheel shot apart stops being a way in, even though the core seat under it survives.
	if (CorePart && !CorePart->IsFunctional())
	{
		OutFailReason = NavalGameplayTags::Fail_NotOperational;
		return false;
	}

	// Reach is measured to this wheel rather than to the vessel's original one: with several
	// helms on a deck, the one the player walked up to is the one that decides.
	const double DistanceSquared = FVector::DistSquared(Candidate->GetActorLocation(), GetActorLocation());
	if (DistanceSquared > FMath::Square(static_cast<double>(InteractionRange)))
	{
		OutFailReason = NavalGameplayTags::Fail_TooFar;
		return false;
	}

	return Helm->CanOccupy(Candidate, OutFailReason);
}

bool ANavalHelmActor::TryOccupy(AActor* NewOperator)
{
	if (!HasAuthority())
	{
		return false;
	}

	FGameplayTag FailReason;
	if (!CanOperate(NewOperator, FailReason))
	{
		UE_LOG(
			LogNavalCore,
			Verbose,
			TEXT("[Helm] Occupy refused helm=%s candidate=%s reason=%s"),
			*GetNameSafe(this),
			*GetNameSafe(NewOperator),
			*FailReason.ToString());
		return false;
	}

	UNavalHelmComponent* Helm = GetHelmComponent();
	return Helm && Helm->TryOccupy(NewOperator);
}

void ANavalHelmActor::ReleaseOperator(AActor* LeavingOperator)
{
	if (!HasAuthority())
	{
		return;
	}

	if (UNavalHelmComponent* Helm = GetHelmComponent())
	{
		Helm->ReleaseHelm(LeavingOperator);
	}
}

FTransform ANavalHelmActor::GetOperatorTransform() const
{
	return OperatorPoint ? OperatorPoint->GetComponentTransform() : GetActorTransform();
}
