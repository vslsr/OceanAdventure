// Copyright Epic Games, Inc. All Rights Reserved.

#include "Raft/RaftBuoyancyComponent.h"

#include "GameFramework/Actor.h"
#include "Math/RotationMatrix.h"
#include "Raft/RaftDefinition.h"
#include "RaftRuntimeModule.h"
#include "World/OceanGenerationSettings.h"
#include "World/OceanWaterSurface.h"
#include "World/OceanWorldManagerComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RaftBuoyancyComponent)

URaftBuoyancyComponent::URaftBuoyancyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(false);

	PontoonOffsets = {
		FVector(90.0, 150.0, 0.0),
		FVector(-90.0, 150.0, 0.0),
		FVector(90.0, -150.0, 0.0),
		FVector(-90.0, -150.0, 0.0)
	};
}

void URaftBuoyancyComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		SetComponentTickEnabled(false);
		return;
	}

	CachedWorldManager = UOceanWorldManagerComponent::Get(this);
}

void URaftBuoyancyComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!bBuoyancyEnabled || !OwnerActor || !OwnerActor->HasAuthority() || !World
		|| PontoonOffsets.IsEmpty())
	{
		return;
	}

	const FRotator CurrentRotation = OwnerActor->GetActorRotation();
	const FQuat YawRotation(FVector::UpVector, FMath::DegreesToRadians(CurrentRotation.Yaw));
	const FVector ActorLocation = OwnerActor->GetActorLocation();
	const float TimeSeconds = static_cast<float>(World->GetTimeSeconds());

	float HeightSum = 0.0f;
	FVector NormalSum = FVector::ZeroVector;
	int32 ValidSampleCount = 0;
	for (const FVector& PontoonOffset : PontoonOffsets)
	{
		const FVector HorizontalOffset = YawRotation.RotateVector(
			FVector(PontoonOffset.X, PontoonOffset.Y, 0.0f));
		const FVector SampleLocation = ActorLocation + HorizontalOffset;

		float SurfaceHeight = 0.0f;
		FVector SurfaceNormal = FVector::UpVector;
		if (SampleWaterSurface(SampleLocation, TimeSeconds, SurfaceHeight, SurfaceNormal))
		{
			HeightSum += SurfaceHeight - PontoonOffset.Z;
			NormalSum += SurfaceNormal;
			++ValidSampleCount;
		}
	}

	if (ValidSampleCount == 0)
	{
		return;
	}

	LastSampledSurfaceHeight = HeightSum / static_cast<float>(ValidSampleCount);
	const FVector AverageNormal = NormalSum.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const FVector YawForward = YawRotation.GetForwardVector();
	const FVector SurfaceForward = FVector::VectorPlaneProject(YawForward, AverageNormal)
		.GetSafeNormal(UE_SMALL_NUMBER, YawForward);
	FRotator TargetRotation = FRotationMatrix::MakeFromXZ(SurfaceForward, AverageNormal).Rotator();
	const float SafeMaxTiltDegrees = FMath::Clamp(MaxTiltDegrees, 0.0f, 20.0f);
	TargetRotation.Pitch = FMath::Clamp(
		TargetRotation.Pitch, -SafeMaxTiltDegrees, SafeMaxTiltDegrees);
	TargetRotation.Roll = FMath::Clamp(
		TargetRotation.Roll, -SafeMaxTiltDegrees, SafeMaxTiltDegrees);
	TargetRotation.Yaw = CurrentRotation.Yaw;

	FVector TargetLocation = ActorLocation;
	TargetLocation.Z = LastSampledSurfaceHeight + WaterlineOffset;

	const bool bSnap = bSnapToSurfaceOnFirstUpdate && !bAppliedInitialSurface;
	const FVector NewLocation = bSnap
		? TargetLocation
		: FVector(
			ActorLocation.X,
			ActorLocation.Y,
			FMath::FInterpTo(ActorLocation.Z, TargetLocation.Z, DeltaTime, VerticalInterpSpeed));
	const FRotator NewRotation = bSnap
		? TargetRotation
		: FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, RotationInterpSpeed);

	// Passengers must never become blockers for the platform that owns their MovementBase.
	// Driving collision will be handled by the later navigation movement layer; buoyancy only
	// changes Z and small pitch/roll here, so a non-swept transform is intentional.
	OwnerActor->SetActorLocationAndRotation(
		NewLocation, NewRotation, false, nullptr, ETeleportType::None);
	bAppliedInitialSurface = true;
}

void URaftBuoyancyComponent::ApplyDefinition(const URaftDefinition* Definition)
{
	if (!Definition)
	{
		return;
	}

	PontoonOffsets = Definition->GetPontoonOffsets();
	WaterlineOffset = Definition->GetWaterlineOffset();
	MaxTiltDegrees = Definition->GetMaxTiltDegrees();
	VerticalInterpSpeed = Definition->GetVerticalInterpSpeed();
	RotationInterpSpeed = Definition->GetRotationInterpSpeed();
}

void URaftBuoyancyComponent::SetBuoyancyEnabled(bool bEnabled)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	bBuoyancyEnabled = bEnabled;
	SetComponentTickEnabled(bEnabled);
}

bool URaftBuoyancyComponent::SampleWaterSurface(
	const FVector& WorldLocation, float TimeSeconds, float& OutHeight, FVector& OutNormal)
{
	if (!CachedWorldManager.IsValid())
	{
		CachedWorldManager = UOceanWorldManagerComponent::Get(this);
	}

	FOceanWaterSurfaceSample Sample;
	if (GenerationSettingsOverride)
	{
		Sample = GenerationSettingsOverride->SampleWaterSurface(
			FVector2D(WorldLocation.X, WorldLocation.Y), TimeSeconds);
	}
	else if (const UOceanWorldManagerComponent* WorldManager = CachedWorldManager.Get())
	{
		Sample = WorldManager->SampleWaterSurface(WorldLocation, TimeSeconds);
	}
	else if (const UOceanGenerationSettings* Settings = ResolveGenerationSettings())
	{
		Sample = Settings->SampleWaterSurface(
			FVector2D(WorldLocation.X, WorldLocation.Y), TimeSeconds);
	}

	if (!Sample.bIsValid)
	{
		return false;
	}

	OutHeight = Sample.Position.Z;
	OutNormal = Sample.Normal;
	return true;
}

const UOceanGenerationSettings* URaftBuoyancyComponent::ResolveGenerationSettings() const
{
	if (GenerationSettingsOverride)
	{
		return GenerationSettingsOverride;
	}

	if (const UOceanWorldManagerComponent* WorldManager = CachedWorldManager.Get())
	{
		if (const UOceanGenerationSettings* Settings = WorldManager->GetGenerationSettings())
		{
			return Settings;
		}
	}

	return GetDefault<UOceanGenerationSettings>();
}
