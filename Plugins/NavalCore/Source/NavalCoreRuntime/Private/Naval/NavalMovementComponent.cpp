// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalMovementComponent.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/OverlapResult.h"
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
	ClearManagedMoveIgnoreActors(ManagedPassengerMoveIgnoreActors);
	ClearManagedMoveIgnoreActors(ManagedAttachedMoveIgnoreActors);

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
	DOREPLIFETIME(UNavalMovementComponent, MovementModel);
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

void UNavalMovementComponent::ClearManagedMoveIgnoreActors(
	TArray<TWeakObjectPtr<AActor>>& ManagedActors)
{
	if (UpdatedPrimitive)
	{
		for (const TWeakObjectPtr<AActor>& IgnoredActor : ManagedActors)
		{
			if (AActor* Actor = IgnoredActor.Get())
			{
				UpdatedPrimitive->IgnoreActorWhenMoving(Actor, false);
			}
		}
	}

	ManagedActors.Reset();
}

void UNavalMovementComponent::UpdateMoveIgnoreActors(float DeltaTime)
{
	if (bAttachedMoveIgnoreDirty)
	{
		// Reclassify atomically: an operator can move between the passenger and attachment sets.
		ClearManagedMoveIgnoreActors(ManagedPassengerMoveIgnoreActors);
		RefreshAttachedMoveIgnoreActors();
		PassengerScanTimeRemaining = 0.0f;
	}

	PassengerScanTimeRemaining -= DeltaTime;
	if (PassengerScanTimeRemaining <= 0.0f)
	{
		RefreshPassengerMoveIgnoreActors();
		PassengerScanTimeRemaining = FMath::Max(0.05f, PassengerScanInterval);
	}
}

void UNavalMovementComponent::RefreshAttachedMoveIgnoreActors()
{
	ClearManagedMoveIgnoreActors(ManagedAttachedMoveIgnoreActors);
	bAttachedMoveIgnoreDirty = false;

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !UpdatedPrimitive)
	{
		return;
	}

	TArray<AActor*> AttachedActors;
	OwnerActor->GetAttachedActors(AttachedActors, /*bResetArray=*/true, /*bRecursively=*/true);
	for (AActor* AttachedActor : AttachedActors)
	{
		if (AttachedActor && !UpdatedPrimitive->GetMoveIgnoreActors().Contains(AttachedActor))
		{
			UpdatedPrimitive->IgnoreActorWhenMoving(AttachedActor, true);
			ManagedAttachedMoveIgnoreActors.Add(AttachedActor);
		}
	}
}

void UNavalMovementComponent::RefreshPassengerMoveIgnoreActors()
{
	ClearManagedMoveIgnoreActors(ManagedPassengerMoveIgnoreActors);

	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!OwnerActor || !World || !UpdatedPrimitive)
	{
		return;
	}

	// Free-moving passengers are based on the deck rather than attached to it. A minimally
	// inflated local overlap finds only touching pawns; the base check then excludes swimmers
	// and actors beside the hull without iterating every pawn in the world.
	TArray<FOverlapResult> PawnOverlaps;
	const FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(NavalPassengerMoveIgnore), /*bTraceComplex=*/false, OwnerActor);
	// CMC deliberately floats walking capsules roughly 2 cm above the floor.
	const FCollisionShape PassengerOverlapShape =
		UpdatedPrimitive->GetCollisionShape(/*Inflation=*/5.0f);
	World->OverlapMultiByObjectType(
		PawnOverlaps,
		UpdatedPrimitive->GetComponentLocation(),
		UpdatedPrimitive->GetComponentQuat(),
		FCollisionObjectQueryParams(ECC_Pawn),
		PassengerOverlapShape,
		QueryParams);

	for (const FOverlapResult& Overlap : PawnOverlaps)
	{
		APawn* Pawn = Cast<APawn>(Overlap.GetActor());
		if (!Pawn)
		{
			continue;
		}

		const UPrimitiveComponent* MovementBase = Pawn->GetMovementBase();
		const AActor* BaseActor = MovementBase ? MovementBase->GetOwner() : nullptr;
		bool bIsPassenger =
			BaseActor == OwnerActor || (BaseActor && BaseActor->IsAttachedTo(OwnerActor));
		if (!bIsPassenger)
		{
			// After leaving a station, CMC may not establish its movement base until the next
			// FindFloor. Keep a pawn still inside the local hull overlap ignored during that gap.
			const FVector LocalPosition = UpdatedPrimitive->GetComponentTransform()
				.InverseTransformPosition(Pawn->GetActorLocation());
			const FVector LocalExtent = PassengerOverlapShape.GetExtent();
			bIsPassenger = FMath::Abs(LocalPosition.X) <= LocalExtent.X
				&& FMath::Abs(LocalPosition.Y) <= LocalExtent.Y
				&& FMath::Abs(LocalPosition.Z) <= LocalExtent.Z;
		}

		if (bIsPassenger)
		{
			if (!UpdatedPrimitive->GetMoveIgnoreActors().Contains(Pawn))
			{
				UpdatedPrimitive->IgnoreActorWhenMoving(Pawn, true);
				ManagedPassengerMoveIgnoreActors.Add(Pawn);
			}
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

void UNavalMovementComponent::SetMovementModel(ENavalMovementModel NewMovementModel)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || MovementModel == NewMovementModel)
	{
		return;
	}

	MovementModel = NewMovementModel;
	Velocity = FVector::ZeroVector;
	CurrentSpeed = 0.0f;
	YawRateDegrees = 0.0f;
	UpdateComponentVelocity();
	OwnerActor->ForceNetUpdate();
}

float UNavalMovementComponent::GetSpeedFraction() const
{
	const float TunedMaximum = MaxForwardSpeed * FMath::Max(0.1f, SpeedScale);
	return TunedMaximum > 0.0f
		? FMath::Clamp(CurrentSpeed / TunedMaximum, -1.0f, 1.0f)
		: 0.0f;
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
	FRotator NewRotation = CurrentRotation;
	float YawDelta = 0.0f;

	if (MovementModel == ENavalMovementModel::DirectPlanar)
	{
		// WASD is already transformed into world space by the local player's ability. The
		// server only clamps magnitude and integrates toward the resulting desired velocity;
		// hull facing never rotates that velocity.
		const FVector2D MoveIntent2D = bHasControl
			? HelmComponent->GetWorldMoveIntent()
			: FVector2D::ZeroVector;
		const FVector MoveIntent = FVector(MoveIntent2D.X, MoveIntent2D.Y, 0.0f)
			.GetClampedToMaxSize(1.0f);
		const FVector TargetVelocity = MoveIntent * ForwardCeiling;
		float VelocityResponse = DirectBrakingDeceleration * Handling.LinearResponse;
		if (!MoveIntent.IsNearlyZero())
		{
			const bool bChangingDirection = !Velocity.IsNearlyZero()
				&& FVector::DotProduct(Velocity.GetSafeNormal2D(), MoveIntent.GetSafeNormal2D()) < 0.25f;
			VelocityResponse = (bChangingDirection
					? DirectDirectionChangeAcceleration
					: DirectAcceleration)
				* Handling.Acceleration
				* Handling.LinearResponse
				* PropulsionCapability;
		}
		Velocity = FMath::VInterpConstantTo(Velocity, TargetVelocity, DeltaTime, VelocityResponse);
		Velocity.Z = 0.0f;

		bool bHasValidFacing = false;
		float FacingYawError = 0.0f;
		if (bHasControl && HelmComponent->HasFacingTarget())
		{
			FVector AimOffset = HelmComponent->GetFacingTarget() - OwnerActor->GetActorLocation();
			AimOffset.Z = 0.0f;
			if (!AimOffset.ContainsNaN()
				&& AimOffset.SizeSquared2D() > FMath::Square(DirectFacingDeadZone))
			{
				bHasValidFacing = true;
				FacingYawError = FMath::FindDeltaAngleDegrees(
					CurrentRotation.Yaw, AimOffset.Rotation().Yaw);
			}
		}

		const float DesiredYawRate = bHasValidFacing
			? FMath::Clamp(
				FacingYawError / FMath::Max(DeltaTime, UE_SMALL_NUMBER),
				-DirectMaxYawRateDegrees,
				DirectMaxYawRateDegrees)
			: 0.0f;
		const float YawResponse = DirectYawAccelerationDegrees
			* Handling.Turn
			* Handling.AngularResponse
			* RudderCapability;
		YawRateDegrees = FMath::FInterpConstantTo(
			YawRateDegrees, DesiredYawRate, DeltaTime, YawResponse);
		YawDelta = YawRateDegrees * DeltaTime;
		if (bHasValidFacing && FMath::Abs(YawDelta) > FMath::Abs(FacingYawError))
		{
			YawDelta = FacingYawError;
			YawRateDegrees = 0.0f;
		}

		NewRotation.Yaw = FRotator::NormalizeAxis(NewRotation.Yaw + YawDelta);
		CurrentSpeed = Velocity.Size2D();
	}
	else
	{
		const float ThrottleIntent = bHasControl ? HelmComponent->GetThrottleIntent() : 0.0f;
		const float SteerIntent = bHasControl ? HelmComponent->GetSteerIntent() : 0.0f;

		// Resolve force in the vessel's current frame. Unlike a target-speed interpolation this
		// preserves momentum while W/S is released and lets the direction lag behind the hull when
		// AD turns it, after which lateral water drag slowly aligns the velocity with the bow.
		float ForwardSpeed = FVector::DotProduct(Velocity, Forward);
		const float AccelerationRate = Acceleration
			* Handling.Acceleration
			* Handling.LinearResponse
			* PropulsionCapability;
		if (FMath::IsNearlyZero(ThrottleIntent))
		{
			ForwardSpeed = FMath::FInterpConstantTo(
				ForwardSpeed,
				0.0f,
				DeltaTime,
				DriftDeceleration * Handling.LinearResponse);
		}
		else
		{
			const bool bOpposingForce = !FMath::IsNearlyZero(ForwardSpeed)
				&& FMath::Sign(ThrottleIntent) != FMath::Sign(ForwardSpeed);
			const float ForceRate = bOpposingForce
				? BrakingDeceleration * Handling.LinearResponse
				: AccelerationRate;
			ForwardSpeed += FMath::Sign(ThrottleIntent) * ForceRate * DeltaTime;
		}

		ForwardSpeed = FMath::Clamp(
			ForwardSpeed,
			-ForwardCeiling * MaxReverseSpeedFraction,
			ForwardCeiling);
		const float SpeedFraction = MaxForwardSpeed > 0.0f
			? FMath::Abs(ForwardSpeed) / (MaxForwardSpeed * FMath::Max(0.1f, SpeedScale))
			: 0.0f;
		const float TurnAuthority = FMath::Lerp(
			MinSpeedFractionForTurn,
			1.0f,
			FMath::InterpEaseInOut(
				0.0f, 1.0f, FMath::Clamp(SpeedFraction, 0.0f, 1.0f), 2.0f));

		const float YawTorque = SteerIntent
			* YawAccelerationDegrees
			* Handling.Turn
			* Handling.AngularResponse
			* RudderCapability
			* TurnAuthority;
		YawRateDegrees += YawTorque * DeltaTime;
		YawRateDegrees = FMath::Clamp(YawRateDegrees, -MaxYawRateDegrees, MaxYawRateDegrees);
		YawRateDegrees = FMath::FInterpTo(
			YawRateDegrees,
			0.0f,
			DeltaTime,
			YawDamping * Handling.AngularResponse);

		YawDelta = YawRateDegrees * DeltaTime;
		NewRotation.Yaw = FRotator::NormalizeAxis(NewRotation.Yaw + YawDelta);

		const FRotator NewYawRotation(0.0f, NewRotation.Yaw, 0.0f);
		const FVector NewForward = NewYawRotation.Vector();
		const FVector NewRight = FRotationMatrix(NewYawRotation).GetUnitAxis(EAxis::Y);

		// Keep the force-integrated world velocity through the yaw change. Only the component
		// perpendicular to the new bow is damped, so the travel direction converges gradually.
		const FVector ForceIntegratedVelocity = Forward * ForwardSpeed
			+ Right * FVector::DotProduct(Velocity, Right);
		const float NewForwardSpeed = FVector::DotProduct(ForceIntegratedVelocity, NewForward);
		const float NewLateralSpeed = FMath::FInterpConstantTo(
			FVector::DotProduct(ForceIntegratedVelocity, NewRight),
			0.0f,
			DeltaTime,
			LateralDrag * Handling.LinearResponse);
		Velocity = NewForward * NewForwardSpeed + NewRight * NewLateralSpeed;
		Velocity.Z = 0.0f;
		CurrentSpeed = FVector::DotProduct(Velocity, NewForward);
	}

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

	UpdateMoveIgnoreActors(DeltaTime);

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
		// distance and any second wall. This P0 event intentionally reports only the first contact.
		SlideAlongSurface(Delta, 1.0f - Hit.Time, BlockingNormal, Hit, /*bHandleImpact=*/false);
		Velocity = FVector::VectorPlaneProject(Velocity, BlockingNormal);
		Velocity.Z = 0.0;
		const FVector NewForward = FRotator(0.0f, NewRotation.Yaw, 0.0f).Vector();
		CurrentSpeed = MovementModel == ENavalMovementModel::DirectPlanar
			? Velocity.Size2D()
			: FVector::DotProduct(Velocity, NewForward);
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

	const float MaxExtrapolation = MovementModel == ENavalMovementModel::DirectPlanar
		? DirectClientMaxExtrapolationSeconds
		: ClientMaxExtrapolationSeconds;
	const float ElapsedSincePose = FMath::Clamp(
		static_cast<float>(World->GetTimeSeconds() - LastReplicatedPoseTime),
		0.0f,
		MaxExtrapolation);
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

	const float LocationHalfLife = MovementModel == ENavalMovementModel::DirectPlanar
		? DirectClientLocationSmoothingHalfLife
		: ClientLocationSmoothingHalfLife;
	const float RotationHalfLife = MovementModel == ENavalMovementModel::DirectPlanar
		? DirectClientRotationSmoothingHalfLife
		: ClientRotationSmoothingHalfLife;
	const float LocationAlpha = bSnapLocation
		? 1.0f
		: 1.0f - FMath::Pow(
			0.5f, DeltaTime / FMath::Max(LocationHalfLife, UE_SMALL_NUMBER));
	const float RotationAlpha = bSnapRotation
		? 1.0f
		: 1.0f - FMath::Pow(
			0.5f, DeltaTime / FMath::Max(RotationHalfLife, UE_SMALL_NUMBER));
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
