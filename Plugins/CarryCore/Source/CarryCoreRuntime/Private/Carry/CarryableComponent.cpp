// Copyright Epic Games, Inc. All Rights Reserved.

#include "Carry/CarryableComponent.h"

#include "Carry/CarrierComponent.h"
#include "Carry/CarryGameplayTags.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CarryableComponent)

UCarryableComponent::UCarryableComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	CarryClass = CarryGameplayTags::Carry_Class_Haulable;
}

void UCarryableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCarryableComponent, Carrier);
	DOREPLIFETIME(UCarryableComponent, RestLocation);
	DOREPLIFETIME(UCarryableComponent, RestYaw);
	DOREPLIFETIME(UCarryableComponent, RestAttachParent);
}

void UCarryableComponent::BeginPlay()
{
	Super::BeginPlay();

	// A late joiner receives Carrier in the same bunch as the component, so the state is
	// already set by the time this runs and has to be applied once here as well.
	if (Carrier != nullptr)
	{
		ApplyCarryState();
	}
	else if (const AActor* Owner = GetOwner())
	{
		// Wherever it was authored or spawned is its resting place until someone moves it,
		// including whatever it was already attached to -- a deck gun starts life on a deck.
		RestLocation = Owner->GetActorLocation();
		RestYaw = Owner->GetActorRotation().Yaw;
		RestAttachParent = Owner->GetRootComponent()
			? Owner->GetRootComponent()->GetAttachParent()
			: nullptr;
	}
}

void UCarryableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// A destroyed gun must not leave the carrier believing its hands are full.
	if (Carrier != nullptr)
	{
		if (UCarrierComponent* CarrierComponent = UCarrierComponent::FindCarrier(Carrier))
		{
			CarrierComponent->NotifyCarryStateChanged(this, false);
		}
	}
	AppliedCarrier.Reset();
	bAppliedCarried = false;

	Super::EndPlay(EndPlayReason);
}

UCarryableComponent* UCarryableComponent::FindCarryable(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UCarryableComponent>() : nullptr;
}

FTransform UCarryableComponent::GetCarryOffsetTransform() const
{
	return FTransform(CarryOffsetRotation, CarryOffsetLocation);
}

bool UCarryableComponent::CanBePickedUpBy(const AActor* Candidate, FGameplayTag& OutFailReason) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Candidate)
	{
		OutFailReason = CarryGameplayTags::Fail_Carry_Invalid;
		return false;
	}
	if (Carrier != nullptr)
	{
		OutFailReason = CarryGameplayTags::Fail_Carry_Occupied;
		return false;
	}

	const double DistanceSquared = FVector::DistSquared(Candidate->GetActorLocation(), Owner->GetActorLocation());
	if (DistanceSquared > FMath::Square(static_cast<double>(PickupRange)))
	{
		OutFailReason = CarryGameplayTags::Fail_Carry_TooFar;
		return false;
	}

	return true;
}

void UCarryableComponent::SetCarrier(AActor* NewCarrier)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Carrier == NewCarrier)
	{
		return;
	}

	Carrier = NewCarrier;
	ApplyCarryState();
	Owner->ForceNetUpdate();
}

void UCarryableComponent::SetRestTransform(
	const FVector& InLocation, float InYaw, USceneComponent* InAttachParent)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	RestLocation = InLocation;
	RestYaw = InYaw;
	RestAttachParent = InAttachParent;
}

void UCarryableComponent::OnRep_CarryState()
{
	ApplyCarryState();
}

void UCarryableComponent::ApplyCarryState()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	UpdateCarrierCache();

	if (Carrier != nullptr)
	{
		FName SocketName = NAME_None;
		USceneComponent* AttachTarget = nullptr;
		FTransform RelativeTransform = GetCarryOffsetTransform();
		if (UCarrierComponent* CarrierComponent = UCarrierComponent::FindCarrier(Carrier))
		{
			AttachTarget = CarrierComponent->ResolveCarryAttachTarget(SocketName);
			RelativeTransform = CarrierComponent->GetCarryRelativeTransform(this);
		}

		if (AttachTarget)
		{
			Owner->AttachToComponent(
				AttachTarget, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
			Owner->SetActorRelativeTransform(RelativeTransform);
		}

		// Carried, so out of the world: nothing traces against it, nothing walks into it, and
		// the station search that finds a gun to man cannot find one hanging off a shoulder.
		Owner->SetActorEnableCollision(false);
	}
	else if (bAppliedCarried)
	{
		Owner->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Owner->SetActorLocationAndRotation(FVector(RestLocation), FRotator(0.0, RestYaw, 0.0));
		if (USceneComponent* AttachParent = RestAttachParent.Get())
		{
			// Set down on something that moves -- a deck, a lift -- so it rides that instead of
			// being left behind in world space the first time the host moves.
			Owner->AttachToComponent(AttachParent, FAttachmentTransformRules::KeepWorldTransform);
		}
		Owner->SetActorEnableCollision(true);
	}

	bAppliedCarried = Carrier != nullptr;
	OnCarryStateChanged.Broadcast(this, Carrier);
}

void UCarryableComponent::UpdateCarrierCache()
{
	AActor* PreviousCarrier = AppliedCarrier.Get();
	if (PreviousCarrier != nullptr && PreviousCarrier != Carrier)
	{
		if (UCarrierComponent* PreviousCarrierComponent = UCarrierComponent::FindCarrier(PreviousCarrier))
		{
			PreviousCarrierComponent->NotifyCarryStateChanged(this, false);
		}
	}

	if (Carrier != nullptr)
	{
		if (UCarrierComponent* CarrierComponent = UCarrierComponent::FindCarrier(Carrier))
		{
			CarrierComponent->NotifyCarryStateChanged(this, true);
		}
	}

	AppliedCarrier = Carrier;
}
