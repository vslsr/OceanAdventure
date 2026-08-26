// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalMovementComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/RotationMatrix.h"
#include "Naval/NavalCoreTypes.h"
#include "Naval/NavalHelmComponent.h"
#include "Naval/NavalLoadComponent.h"
#include "Naval/NavalVesselComponent.h"
#include "NavalCoreRuntimeModule.h"
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
	EnsureOwnerCanMove();
	PlanarVelocity = FVector::ZeroVector;
	YawRateDegrees = 0.0f;

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

void UNavalMovementComponent::EnsureOwnerCanMove()
{
	AActor* OwnerActor = GetOwner();
	USceneComponent* RootComponent = OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
	if (!RootComponent || RootComponent->Mobility == EComponentMobility::Movable)
	{
		return;
	}

	UE_LOG(
		LogNavalCore,
		Warning,
		TEXT("[Movement] %s : %s was not Movable, so every steering and buoyancy write was being "
			 "discarded. Forcing Movable -- clear the stale Mobility on the Blueprint or the "
			 "placed instance so this stops being needed."),
		*GetPathNameSafe(OwnerActor),
		*RootComponent->GetName());
	RootComponent->SetMobility(EComponentMobility::Movable);
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

	// Vessel components arrive by GameFeature injection, so a peer can finish registering after
	// this one's BeginPlay. Re-resolving only while the helm is missing keeps a boat that came
	// up in an awkward order from being permanently unsteerable for one lookup a frame.
	if (!Helm.IsValid())
	{
		ResolvePeers();
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
	const FRotator CurrentRotation = OwnerActor->GetActorRotation();
	const FRotator CurrentYawRotation(0.0f, CurrentRotation.Yaw, 0.0f);
	const FVector Forward = CurrentYawRotation.Vector();
	const FVector Right = FRotationMatrix(CurrentYawRotation).GetUnitAxis(EAxis::Y);

	// Resolve force in the vessel's current frame. Unlike a target-speed interpolation this
	// preserves momentum while W/S is released and lets the direction lag behind the hull when
	// AD turns it, after which lateral water drag slowly aligns the velocity with the bow.
	float ForwardSpeed = FVector::DotProduct(PlanarVelocity, Forward);
	const float AccelerationRate = Acceleration * Handling.Acceleration * PropulsionCapability;
	if (FMath::IsNearlyZero(ThrottleIntent))
	{
		ForwardSpeed = FMath::FInterpConstantTo(ForwardSpeed, 0.0f, DeltaTime, DriftDeceleration);
	}
	else
	{
		const bool bOpposingForce = !FMath::IsNearlyZero(ForwardSpeed)
			&& FMath::Sign(ThrottleIntent) != FMath::Sign(ForwardSpeed);
		const float ForceRate = bOpposingForce ? BrakingDeceleration : AccelerationRate;
		ForwardSpeed += FMath::Sign(ThrottleIntent) * ForceRate * DeltaTime;
	}

	ForwardSpeed = FMath::Clamp(
		ForwardSpeed,
		-ForwardCeiling * MaxReverseSpeedFraction,
		ForwardCeiling);
	const float SpeedFraction = MaxForwardSpeed > 0.0f
		? FMath::Abs(ForwardSpeed) / (MaxForwardSpeed * FMath::Max(0.1f, SpeedScale))
		: 0.0f;
	const float TurnAuthority = FMath::Max(
		MinSpeedFractionForTurn, FMath::Clamp(SpeedFraction, 0.0f, 1.0f));

	// Apply AD as torque, then angular drag. Because this path does not depend on speed, A/D
	// still rotates a stationary boat; at speed the same torque produces a gradual heading
	// transition rather than a frame-local snap.
	const float YawTorque = SteerIntent
		* YawAccelerationDegrees
		* Handling.Turn
		* RudderCapability
		* TurnAuthority;
	YawRateDegrees += YawTorque * DeltaTime;
	YawRateDegrees = FMath::Clamp(YawRateDegrees, -MaxYawRateDegrees, MaxYawRateDegrees);
	YawRateDegrees = FMath::FInterpTo(YawRateDegrees, 0.0f, DeltaTime, YawDamping);

	const float YawDelta = YawRateDegrees * DeltaTime;
	FRotator NewRotation = CurrentRotation;
	NewRotation.Yaw = FRotator::NormalizeAxis(NewRotation.Yaw + YawDelta);

	const FRotator NewYawRotation(0.0f, NewRotation.Yaw, 0.0f);
	const FVector NewForward = NewYawRotation.Vector();
	const FVector NewRight = FRotationMatrix(NewYawRotation).GetUnitAxis(EAxis::Y);

	// Keep the force-integrated world velocity through the yaw change. Only the component
	// perpendicular to the new bow is damped, so the travel direction converges gradually
	// instead of rotating by the full hull yaw every frame.
	const FVector ForceIntegratedVelocity = Forward * ForwardSpeed
		+ Right * FVector::DotProduct(PlanarVelocity, Right);
	const float NewForwardSpeed = FVector::DotProduct(ForceIntegratedVelocity, NewForward);
	const float NewLateralSpeed = FMath::FInterpConstantTo(
		FVector::DotProduct(ForceIntegratedVelocity, NewRight), 0.0f, DeltaTime, LateralDrag);
	PlanarVelocity = NewForward * NewForwardSpeed + NewRight * NewLateralSpeed;
	CurrentSpeed = FVector::DotProduct(PlanarVelocity, NewForward);

	if (PlanarVelocity.IsNearlyZero(0.5f) && FMath::IsNearlyZero(YawDelta, 0.001f))
	{
		CurrentSpeed = 0.0f;
		PlanarVelocity = FVector::ZeroVector;
		return;
	}

	const FVector NewLocation = OwnerActor->GetActorLocation() + PlanarVelocity * DeltaTime;

	// Not swept, matching the host's buoyancy: passengers must never become blockers for the
	// platform that carries them. Hull-to-hull collision belongs to a later ramming pass.
	OwnerActor->SetActorLocationAndRotation(NewLocation, NewRotation, false, nullptr, ETeleportType::None);
}
