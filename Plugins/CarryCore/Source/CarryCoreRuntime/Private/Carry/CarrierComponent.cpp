// Copyright Epic Games, Inc. All Rights Reserved.

#include "Carry/CarrierComponent.h"

#include "Carry/CarryableComponent.h"
#include "Carry/CarryGameplayTags.h"
#include "CollisionShape.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CarrierComponent)

UCarrierComponent::UCarrierComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCarrierComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UCarrierComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Dying and disconnecting must not take a gun out of the world with them: on the server
	// the object goes back on the ground where the carrier stood. A level teardown is not
	// that case -- the whole world is going away, and querying it here would be pointless.
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		PutDownOrRelease();
	}
	else
	{
		Carried.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

UCarrierComponent* UCarrierComponent::FindCarrier(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UCarrierComponent>() : nullptr;
}

void UCarrierComponent::NotifyCarryStateChanged(UCarryableComponent* Carryable, bool bNowCarriedByUs)
{
	if (bNowCarriedByUs)
	{
		Carried = Carryable;
	}
	else if (Carried.Get() == Carryable)
	{
		Carried.Reset();
	}
}

USceneComponent* UCarrierComponent::ResolveCarryAttachTarget(FName& OutSocketName) const
{
	OutSocketName = NAME_None;

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	if (const ACharacter* OwningCharacter = Cast<ACharacter>(Owner))
	{
		USkeletalMeshComponent* Mesh = OwningCharacter->GetMesh();
		if (Mesh && !CarrySocketName.IsNone() && Mesh->DoesSocketExist(CarrySocketName))
		{
			OutSocketName = CarrySocketName;
			return Mesh;
		}
	}

	// No authored socket yet: hold it off the capsule so a greybox character can still carry.
	return Owner->GetRootComponent();
}

FTransform UCarrierComponent::GetCarryRelativeTransform(const UCarryableComponent* Carryable) const
{
	const FTransform CarryableOffset = Carryable ? Carryable->GetCarryOffsetTransform() : FTransform::Identity;

	FName SocketName = NAME_None;
	ResolveCarryAttachTarget(SocketName);
	if (!SocketName.IsNone())
	{
		return CarryableOffset;
	}

	return CarryableOffset * FTransform(CarryFallbackRotation, CarryFallbackLocation);
}

UCarryableComponent* UCarrierComponent::FindBestCarryTarget() const
{
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World || IsCarrying())
	{
		return nullptr;
	}

	// An overlap on a key press, in the same shape the naval station search uses: this runs
	// when a player asks for something, never per frame.
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		Owner->GetActorLocation(),
		FQuat::Identity,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(FMath::Max(10.0f, SearchRadius)));

	UCarryableComponent* Nearest = nullptr;
	double NearestDistanceSquared = TNumericLimits<double>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const AActor* Candidate = Overlap.GetActor();
		UCarryableComponent* Carryable = UCarryableComponent::FindCarryable(Candidate);
		FGameplayTag FailReason;
		if (!Carryable || !CanPickUp(Carryable, FailReason))
		{
			continue;
		}

		const double DistanceSquared =
			FVector::DistSquared(Owner->GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSquared < NearestDistanceSquared)
		{
			NearestDistanceSquared = DistanceSquared;
			Nearest = Carryable;
		}
	}

	return Nearest;
}

bool UCarrierComponent::CanPickUp(const UCarryableComponent* Carryable, FGameplayTag& OutFailReason) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Carryable)
	{
		OutFailReason = CarryGameplayTags::Fail_Carry_Invalid;
		return false;
	}
	if (IsCarrying())
	{
		OutFailReason = CarryGameplayTags::Fail_Carry_HandsFull;
		return false;
	}

	return Carryable->CanBePickedUpBy(Owner, OutFailReason);
}

bool UCarrierComponent::CanPutDown(FGameplayTag& OutFailReason) const
{
	if (!GetOwner())
	{
		OutFailReason = CarryGameplayTags::Fail_Carry_Invalid;
		return false;
	}

	const UCarryableComponent* Carryable = Carried.Get();
	if (!Carryable)
	{
		OutFailReason = CarryGameplayTags::Fail_Carry_NotCarrying;
		return false;
	}
	if (!Carryable->GetOwner())
	{
		OutFailReason = CarryGameplayTags::Fail_Carry_Invalid;
		return false;
	}

	return true;
}

bool UCarrierComponent::ServerPickUp(UCarryableComponent* Carryable)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return false;
	}

	// The client asked; this is the same check again, against the server's own state.
	FGameplayTag FailReason;
	if (!CanPickUp(Carryable, FailReason))
	{
		return false;
	}

	Carryable->SetCarrier(Owner);
	return Carried.Get() == Carryable;
}

bool UCarrierComponent::ServerPutDown()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return false;
	}

	FGameplayTag FailReason;
	if (!CanPutDown(FailReason))
	{
		return false;
	}

	UCarryableComponent* Carryable = Carried.Get();
	AActor* CarriedActor = Carryable->GetOwner();
	const FVector PutDownLocation = ComputePutDownLocation(CarriedActor);

	// Rest transform first: clearing the carrier is what makes every machine place the
	// object, so it has to already know where.
	Carryable->SetRestTransform(PutDownLocation, Owner->GetActorRotation().Yaw);
	Carryable->SetCarrier(nullptr);

	// Collision came back with the put-down, so anyone standing in the new footprint is
	// standing inside a blocking box. Move them out rather than letting the solver decide.
	PushCharactersClear(CarriedActor, PutDownLocation, Carryable->GetPutDownClearanceRadius());
	return true;
}

FVector UCarrierComponent::ComputePutDownLocation(const AActor* CarriedActor) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}

	FVector Forward = Owner->GetActorForwardVector();
	Forward.Z = 0.0;
	Forward = Forward.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}

	// Feet, not eyes: the gun's own origin sits on the ground between its legs.
	FVector FootLocation = Owner->GetActorLocation();
	if (const APawn* OwningPawn = Cast<APawn>(Owner))
	{
		FootLocation.Z -= OwningPawn->GetSimpleCollisionHalfHeight();
	}

	const FVector Desired = FootLocation + Forward * PutDownForwardDistance;

	const UWorld* World = GetWorld();
	if (!World)
	{
		return Desired;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CarryPutDownGround), /*bTraceComplex=*/false);
	QueryParams.AddIgnoredActor(Owner);
	if (CarriedActor)
	{
		QueryParams.AddIgnoredActor(CarriedActor);
	}

	FHitResult Hit;
	const FVector TraceStart = Desired + FVector(0.0, 0.0, PutDownTraceUp);
	const FVector TraceEnd = Desired - FVector(0.0, 0.0, PutDownTraceDown);
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, GroundTraceChannel, QueryParams))
	{
		return Hit.ImpactPoint;
	}

	// Nothing under it -- a pier edge, deep water, a hole. V1 leaves it at foot height and
	// lets the object stand where the player is standing; legality of the spot is the
	// deploy rules' job, and it is the next version's problem.
	return Desired;
}

void UCarrierComponent::PushCharactersClear(
	const AActor* PlacedActor, const FVector& Center, float ClearanceRadius) const
{
	const UWorld* World = GetWorld();
	if (!World || ClearanceRadius <= 0.0f)
	{
		return;
	}

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		Center,
		FQuat::Identity,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(ClearanceRadius));

	const AActor* Owner = GetOwner();
	TSet<AActor*> Pushed;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* OverlapActor = Overlap.GetActor();
		if (!OverlapActor || OverlapActor == PlacedActor || Pushed.Contains(OverlapActor))
		{
			continue;
		}

		APawn* OverlapPawn = Cast<APawn>(OverlapActor);
		if (!OverlapPawn)
		{
			continue;
		}
		Pushed.Add(OverlapActor);

		FVector Away = OverlapPawn->GetActorLocation() - Center;
		Away.Z = 0.0;
		if (Away.IsNearlyZero())
		{
			// Standing dead centre: send them back the way the carrier is facing, away from
			// the object rather than in an arbitrary direction.
			Away = Owner ? -Owner->GetActorForwardVector() : FVector::BackwardVector;
			Away.Z = 0.0;
		}
		const FVector Direction = Away.GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			continue;
		}

		if (ACharacter* OverlapCharacter = Cast<ACharacter>(OverlapPawn))
		{
			// A horizontal launch rather than a teleport: character movement resolves the
			// remaining penetration itself, and the player sees themselves step back instead
			// of snapping.
			OverlapCharacter->LaunchCharacter(
				Direction * PutDownPushAwaySpeed, /*bXYOverride=*/true, /*bZOverride=*/false);
		}
		else
		{
			OverlapPawn->AddActorWorldOffset(Direction * ClearanceRadius, /*bSweep=*/true);
		}
	}
}

void UCarrierComponent::PutDownOrRelease()
{
	UCarryableComponent* Carryable = Carried.Get();
	if (!Carryable)
	{
		return;
	}

	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority() && Carryable->GetOwner())
	{
		ServerPutDown();
		return;
	}

	Carried.Reset();
}
