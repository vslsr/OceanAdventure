// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalMovementComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Naval/NavalCoreTypes.h"
#include "Naval/NavalHelmComponent.h"
#include "Naval/NavalLoadComponent.h"
#include "Naval/NavalVesselComponent.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalMovementComponent)

UNavalMovementComponent::UNavalMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Same group as buoyancy: both write the host transform before physics, on different axes.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void UNavalMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolvePeers();

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		// Clients receive the replicated transform; predicting a shared platform locally would
		// fight the passengers standing on it.
		SetComponentTickEnabled(false);
	}
}

void UNavalMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UNavalMovementComponent, CurrentSpeed);
	DOREPLIFETIME(UNavalMovementComponent, SpeedScale);
}

void UNavalMovementComponent::ResolvePeers()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	Helm = OwnerActor->FindComponentByClass<UNavalHelmComponent>();
	Load = OwnerActor->FindComponentByClass<UNavalLoadComponent>();
	Vessel = OwnerActor->FindComponentByClass<UNavalVesselComponent>();
}

void UNavalMovementComponent::SetSpeedScale(float NewSpeedScale)
{
	if (AActor* OwnerActor = GetOwner(); OwnerActor && OwnerActor->HasAuthority())
	{
		SpeedScale = FMath::Clamp(NewSpeedScale, 0.1f, 2.0f);
		OwnerActor->ForceNetUpdate();
	}
}

float UNavalMovementComponent::GetSpeedFraction() const
{
	return MaxForwardSpeed > 0.0f ? FMath::Clamp(CurrentSpeed / MaxForwardSpeed, -1.0f, 1.0f) : 0.0f;
}

void UNavalMovementComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || DeltaTime <= 0.0f)
	{
		return;
	}

	UNavalHelmComponent* HelmComponent = Helm.Get();
	const UNavalLoadComponent* LoadComponent = Load.Get();
	const UNavalVesselComponent* VesselComponent = Vessel.Get();

	FNavalHandlingScalars Handling;
	if (LoadComponent)
	{
		Handling = LoadComponent->GetHandlingScalars();
	}

	// A wreck keeps no active control at all; it just bleeds off what it had.
	const bool bWrecked = VesselComponent && VesselComponent->GetVesselState() == ENavalVesselState::Wreck;
	const bool bHasControl = !bWrecked && HelmComponent && HelmComponent->AcceptsControlInput();

	const float ThrottleIntent = bHasControl ? HelmComponent->GetThrottleIntent() : 0.0f;
	const float SteerIntent = bHasControl ? HelmComponent->GetSteerIntent() : 0.0f;

	// 舵芯只负责接收并分发移动指令: what it can actually deliver comes from the parts.
	const float PropulsionCapability = VesselComponent
		? FMath::Lerp(EmergencyPropulsionFraction, 1.0f, VesselComponent->GetPartCapability(ENavalPartType::Propulsion))
		: 1.0f;
	const float RudderCapability = VesselComponent
		? FMath::Lerp(EmergencyRudderFraction, 1.0f, VesselComponent->GetPartCapability(ENavalPartType::Rudder))
		: 1.0f;
	const float CoreScale = (HelmComponent && HelmComponent->GetHelmState() == ENavalHelmState::Damaged)
		? DamagedCoreSpeedScale
		: 1.0f;

	const float ForwardCeiling =
		MaxForwardSpeed * SpeedScale * Handling.TopSpeed * PropulsionCapability * CoreScale;
	const float TargetSpeed = ThrottleIntent >= 0.0f
		? ForwardCeiling * ThrottleIntent
		: ForwardCeiling * MaxReverseSpeedFraction * ThrottleIntent;

	const bool bCoasting = FMath::IsNearlyZero(ThrottleIntent);
	const float RateTowardsTarget = bCoasting
		? DriftDeceleration
		: (FMath::Abs(TargetSpeed) > FMath::Abs(CurrentSpeed)
			? Acceleration * Handling.Acceleration * PropulsionCapability
			: BrakingDeceleration);
	CurrentSpeed = FMath::FInterpConstantTo(
		CurrentSpeed, TargetSpeed, DeltaTime, FMath::Max(1.0f, RateTowardsTarget));

	const float SpeedFraction = MaxForwardSpeed > 0.0f
		? FMath::Abs(CurrentSpeed) / (MaxForwardSpeed * FMath::Max(0.1f, SpeedScale))
		: 0.0f;
	// Rudders bite against water flow, so a nearly stopped hull barely answers the wheel.
	const float TurnAuthority = FMath::Clamp(SpeedFraction / FMath::Max(0.01f, MinSpeedFractionForTurn), 0.0f, 1.0f);
	const float YawDelta = MaxYawRateDegrees
		* Handling.Turn
		* RudderCapability
		* TurnAuthority
		* SteerIntent
		* FMath::Sign(CurrentSpeed == 0.0f ? 1.0f : CurrentSpeed)
		* DeltaTime;

	if (FMath::IsNearlyZero(CurrentSpeed, 0.5f) && FMath::IsNearlyZero(YawDelta, 0.001f))
	{
		CurrentSpeed = 0.0f;
		return;
	}

	FRotator NewRotation = OwnerActor->GetActorRotation();
	NewRotation.Yaw = FRotator::NormalizeAxis(NewRotation.Yaw + YawDelta);

	const FVector Forward = FRotator(0.0f, NewRotation.Yaw, 0.0f).Vector();
	const FVector NewLocation = OwnerActor->GetActorLocation() + Forward * (CurrentSpeed * DeltaTime);

	// Not swept, matching the host's buoyancy: passengers must never become blockers for the
	// platform that carries them. Hull-to-hull collision belongs to a later ramming pass.
	OwnerActor->SetActorLocationAndRotation(NewLocation, NewRotation, false, nullptr, ETeleportType::None);
}
