// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalMovementComponent.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
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
	// This component owns only XY movement. Penetration recovery must not steal Z from buoyancy.
	SetPlaneConstraintNormal(FVector::UpVector);
	SetPlaneConstraintEnabled(true);
	SetIsReplicatedByDefault(true);
}

void UNavalMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolvePeers();
	EnsureOwnerCanMove();
	Velocity = FVector::ZeroVector;
	YawRateDegrees = 0.0f;

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	SetUpdatedComponent(OwnerActor->GetRootComponent());
	if (!UpdatedPrimitive)
	{
		UE_LOG(
			LogNavalCore,
			Error,
			TEXT("[Movement] %s : root component %s is not a UPrimitiveComponent; safe hull "
				 "movement is disabled."),
			*GetPathNameSafe(OwnerActor),
			*GetNameSafe(OwnerActor->GetRootComponent()));
	}

	// Engine movement replication snaps ordinary Actors. While this component is present it
	// publishes one coherent pose sample instead, then restores the host setting on teardown.
	bPreviousOwnerReplicateMovement = OwnerActor->IsReplicatingMovement();
	bChangedOwnerReplicateMovement = bPreviousOwnerReplicateMovement;
	OwnerActor->SetReplicateMovement(false);

	if (OwnerActor->HasAuthority())
	{
		PublishAuthorityPose();
		OwnerActor->ForceNetUpdate();
	}
}

void UNavalMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearManagedMoveIgnoreActors();

	if (bChangedOwnerReplicateMovement)
	{
		if (AActor* OwnerActor = GetOwner())
		{
			OwnerActor->SetReplicateMovement(bPreviousOwnerReplicateMovement);
			if (OwnerActor->HasAuthority())
			{
				OwnerActor->ForceNetUpdate();
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UNavalMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UNavalMovementComponent, CurrentSpeed);
	DOREPLIFETIME(UNavalMovementComponent, SpeedScale);
	DOREPLIFETIME(UNavalMovementComponent, ReplicatedPose);
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

void UNavalMovementComponent::ClearManagedMoveIgnoreActors()
{
	if (UpdatedPrimitive)
	{
		for (const TWeakObjectPtr<AActor>& IgnoredActor : ManagedMoveIgnoreActors)
		{
			if (AActor* Actor = IgnoredActor.Get())
			{
				UpdatedPrimitive->IgnoreActorWhenMoving(Actor, false);
			}
		}
	}

	ManagedMoveIgnoreActors.Reset();
}

void UNavalMovementComponent::RefreshMoveIgnoreActors()
{
	ClearManagedMoveIgnoreActors();

	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!OwnerActor || !World || !UpdatedPrimitive)
	{
		return;
	}

	const auto AddManagedIgnore = [this](AActor* Actor)
	{
		if (!Actor || UpdatedPrimitive->GetMoveIgnoreActors().Contains(Actor))
		{
			return;
		}

		UpdatedPrimitive->IgnoreActorWhenMoving(Actor, true);
		ManagedMoveIgnoreActors.Add(Actor);
	};

	TArray<AActor*> AttachedActors;
	OwnerActor->GetAttachedActors(AttachedActors, /*bResetArray=*/true, /*bRecursively=*/true);
	for (AActor* AttachedActor : AttachedActors)
	{
		AddManagedIgnore(AttachedActor);
	}

	// Free-moving passengers are based on the deck rather than attached to it. A minimally
	// inflated local overlap finds only touching pawns; the base check then excludes swimmers
	// and actors beside the hull without iterating every pawn in the world.
	TArray<FOverlapResult> PawnOverlaps;
	const FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(NavalPassengerMoveIgnore), /*bTraceComplex=*/false, OwnerActor);
	World->OverlapMultiByObjectType(
		PawnOverlaps,
		UpdatedPrimitive->GetComponentLocation(),
		UpdatedPrimitive->GetComponentQuat(),
		FCollisionObjectQueryParams(ECC_Pawn),
		// CMC deliberately floats walking capsules roughly 2 cm above the floor.
		UpdatedPrimitive->GetCollisionShape(/*Inflation=*/5.0f),
		QueryParams);

	for (const FOverlapResult& Overlap : PawnOverlaps)
	{
		APawn* Pawn = Cast<APawn>(Overlap.GetActor());
		const UPrimitiveComponent* MovementBase = Pawn ? Pawn->GetMovementBase() : nullptr;
		const AActor* BaseActor = MovementBase ? MovementBase->GetOwner() : nullptr;
		if (BaseActor == OwnerActor || (BaseActor && BaseActor->IsAttachedTo(OwnerActor)))
		{
			AddManagedIgnore(Pawn);
		}
	}
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
		TEXT("[Movement] %s : %s was not Movable, so steering and buoyancy writes were "
			 "discarded. Forcing Movable; clear the stale Blueprint or instance override."),
		*GetPathNameSafe(OwnerActor),
		*RootComponent->GetName());
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void UNavalMovementComponent::PublishAuthorityPose()
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	ReplicatedPose.Location = OwnerActor->GetActorLocation();
	ReplicatedPose.Rotation = OwnerActor->GetActorRotation();
	ReplicatedPose.PlanarVelocity = Velocity;
	ReplicatedPose.YawRateDegrees = YawRateDegrees;
}

void UNavalMovementComponent::OnRep_ReplicatedPose()
{
	const UWorld* World = GetWorld();
	LastReplicatedPoseTime = World ? World->GetTimeSeconds() : 0.0;
	Velocity = FVector(ReplicatedPose.PlanarVelocity);
	Velocity.Z = 0.0;
	UpdateComponentVelocity();
	bHasReplicatedPose = true;
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
	if (!OwnerActor || DeltaTime <= 0.0f)
	{
		return;
	}

	if (!OwnerActor->HasAuthority())
	{
		TickClientInterpolation(*OwnerActor, DeltaTime);
		return;
	}

	// GameFeature component injection order is not deterministic. Retry only while a required
	// peer is missing, so an awkward registration order cannot permanently disable steering.
	if (!Helm.IsValid() || !Load.IsValid() || !Vessel.IsValid())
	{
		ResolvePeers();
	}

	TickAuthorityMovement(DeltaTime);
	// Publish even while planar motion is idle: buoyancy still changes Z, pitch and roll.
	PublishAuthorityPose();
}

void UNavalMovementComponent::TickAuthorityMovement(float DeltaTime)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
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
	const FRotator CurrentRotation = OwnerActor->GetActorRotation();
	const FRotator CurrentYawRotation(0.0f, CurrentRotation.Yaw, 0.0f);
	const FVector Forward = CurrentYawRotation.Vector();
	const FVector Right = FRotationMatrix(CurrentYawRotation).GetUnitAxis(EAxis::Y);

	// Resolve force in the vessel's current frame. Unlike a target-speed interpolation this
	// preserves momentum while W/S is released and lets the direction lag behind the hull when
	// AD turns it, after which lateral water drag slowly aligns the velocity with the bow.
	float ForwardSpeed = FVector::DotProduct(Velocity, Forward);
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
		+ Right * FVector::DotProduct(Velocity, Right);
	const float NewForwardSpeed = FVector::DotProduct(ForceIntegratedVelocity, NewForward);
	const float NewLateralSpeed = FMath::FInterpConstantTo(
		FVector::DotProduct(ForceIntegratedVelocity, NewRight), 0.0f, DeltaTime, LateralDrag);
	Velocity = NewForward * NewForwardSpeed + NewRight * NewLateralSpeed;
	Velocity.Z = 0.0;
	CurrentSpeed = FVector::DotProduct(Velocity, NewForward);

	if (Velocity.IsNearlyZero(0.5f) && FMath::IsNearlyZero(YawDelta, 0.001f))
	{
		CurrentSpeed = 0.0f;
		Velocity = FVector::ZeroVector;
		UpdateComponentVelocity();
		return;
	}

	if (!UpdatedPrimitive)
	{
		Velocity = FVector::ZeroVector;
		CurrentSpeed = 0.0f;
		return;
	}

	RefreshMoveIgnoreActors();

	const FVector Delta = Velocity * DeltaTime;
	FHitResult Hit(1.0f);
	SafeMoveUpdatedComponent(Delta, NewRotation.Quaternion(), /*bSweep=*/true, Hit);
	if (Hit.IsValidBlockingHit())
	{
		const FVector BlockingNormal = Hit.Normal;
		const FVector BlockingImpactNormal = Hit.ImpactNormal;
		const float ImpactSpeed = FMath::Max(
			0.0f, FVector::DotProduct(Velocity, -BlockingImpactNormal));
		OnHullBlocked.Broadcast(Hit, ImpactSpeed);

		// SafeMove has already consumed Hit.Time of Delta. Let the engine resolve the remaining
		// distance and any second wall, then remove the blocked velocity for the next frame.
		SlideAlongSurface(Delta, 1.0f - Hit.Time, BlockingNormal, Hit, /*bHandleImpact=*/false);
		Velocity = FVector::VectorPlaneProject(Velocity, BlockingImpactNormal);
		Velocity.Z = 0.0;
		CurrentSpeed = FVector::DotProduct(Velocity, NewForward);
	}

	// Rotation is still applied directly by MoveComponent. A later overlap pass can reject yaw
	// without changing buoyancy or client interpolation ownership.
	UpdateComponentVelocity();
}

void UNavalMovementComponent::TickClientInterpolation(AActor& OwnerActor, float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World || !bHasReplicatedPose)
	{
		return;
	}

	const float ElapsedSincePose = FMath::Clamp(
		static_cast<float>(World->GetTimeSeconds() - LastReplicatedPoseTime),
		0.0f,
		ClientMaxExtrapolationSeconds);
	const FVector TargetLocation = FVector(ReplicatedPose.Location)
		+ FVector(ReplicatedPose.PlanarVelocity) * ElapsedSincePose;
	FRotator TargetRotation = ReplicatedPose.Rotation;
	// Only powered yaw is safe to carry forward. Wave-authored pitch and roll are interpolated.
	TargetRotation.Yaw = FRotator::NormalizeAxis(
		TargetRotation.Yaw + ReplicatedPose.YawRateDegrees * ElapsedSincePose);
	const FQuat TargetQuat = TargetRotation.Quaternion();

	const FVector CurrentLocation = OwnerActor.GetActorLocation();
	const FQuat CurrentQuat = OwnerActor.GetActorQuat();
	const float DistanceToTarget = FVector::Distance(CurrentLocation, TargetLocation);
	const float AngleToTargetDegrees = FMath::RadiansToDegrees(
		CurrentQuat.AngularDistance(TargetQuat));
	const bool bSnapLocation = ClientSnapDistance <= 0.0f || DistanceToTarget >= ClientSnapDistance;
	const bool bSnapRotation = ClientSnapAngleDegrees <= 0.0f
		|| AngleToTargetDegrees >= ClientSnapAngleDegrees;

	const float LocationAlpha = bSnapLocation
		? 1.0f
		: 1.0f - FMath::Pow(
			0.5f, DeltaTime / FMath::Max(ClientLocationSmoothingHalfLife, UE_SMALL_NUMBER));
	const float RotationAlpha = bSnapRotation
		? 1.0f
		: 1.0f - FMath::Pow(
			0.5f, DeltaTime / FMath::Max(ClientRotationSmoothingHalfLife, UE_SMALL_NUMBER));
	const FVector NewLocation = FMath::Lerp(
		CurrentLocation, TargetLocation, FMath::Clamp(LocationAlpha, 0.0f, 1.0f));
	const FQuat NewRotation = FQuat::Slerp(
		CurrentQuat, TargetQuat, FMath::Clamp(RotationAlpha, 0.0f, 1.0f)).GetNormalized();

	OwnerActor.SetActorLocationAndRotation(
		NewLocation,
		NewRotation,
		false,
		nullptr,
		(bSnapLocation || bSnapRotation) ? ETeleportType::TeleportPhysics : ETeleportType::None);
}
