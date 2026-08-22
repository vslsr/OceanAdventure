// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalFireWindowComponent.h"

#include "GameFramework/Actor.h"
#include "Naval/NavalPartComponent.h"
#include "Naval/NavalTeamStatics.h"
#include "Naval/NavalVesselComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalFireWindowComponent)

UNavalFireWindowComponent::UNavalFireWindowComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Configuration comes from the piece definition on both sides, so nothing here replicates.
	SetIsReplicatedByDefault(false);
}

void UNavalFireWindowComponent::Configure(
	float InOutwardHalfAngleDegrees, float InInnerAuthorizedDepth, float InOpenWindupSeconds)
{
	OutwardHalfAngleDegrees = FMath::Clamp(InOutwardHalfAngleDegrees, 5.0f, 89.0f);
	InnerAuthorizedDepth = FMath::Max(10.0f, InInnerAuthorizedDepth);
	OpenWindupSeconds = FMath::Max(0.0f, InOpenWindupSeconds);
}

int32 UNavalFireWindowComponent::GetOwningTeamId() const
{
	// Ownership is read from the vessel, never from whoever happens to be standing here:
	// design 7.7 is explicit that standing at a window grants an enemy nothing.
	if (const UNavalVesselComponent* Vessel = UNavalVesselComponent::FindVessel(GetOwner()))
	{
		return Vessel->GetTeamId();
	}
	return NavalTeam::GetTeamId(GetOwner());
}

FVector UNavalFireWindowComponent::GetOutwardNormal() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor ? OwnerActor->GetActorForwardVector() : FVector::ForwardVector;
}

FVector UNavalFireWindowComponent::GetWindowLocation() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;
}

bool UNavalFireWindowComponent::IsIntact() const
{
	const AActor* OwnerActor = GetOwner();
	const UNavalPartComponent* Part = OwnerActor ? OwnerActor->FindComponentByClass<UNavalPartComponent>() : nullptr;
	// No part means the window was authored without durability; treat it as solid.
	return !Part || Part->IsPhysicallyPresent();
}

bool UNavalFireWindowComponent::AllowsShot(
	const FVector& ShotStart, const FVector& ShotDirection, int32 ShooterTeamId) const
{
	const int32 WindowTeam = GetOwningTeamId();
	if (!NavalTeam::IsValidTeam(WindowTeam) || WindowTeam != ShooterTeamId)
	{
		return false;
	}

	// A window under construction has a frame but no working shutter, so it does not yet
	// grant the firing exception -- it only blocks, like the wall it is becoming.
	const AActor* OwnerActor = GetOwner();
	const UNavalPartComponent* Part = OwnerActor ? OwnerActor->FindComponentByClass<UNavalPartComponent>() : nullptr;
	if (Part && !Part->IsFunctional())
	{
		return false;
	}

	const FVector OutwardNormal = GetOutwardNormal();
	const FVector ToStart = ShotStart - GetWindowLocation();
	const double DepthBehindWindow = -FVector::DotProduct(ToStart, OutwardNormal);

	// Inside means behind the window, and not so far behind that a shooter across the deck
	// could borrow a window several cells away.
	if (DepthBehindWindow <= 0.0 || DepthBehindWindow > InnerAuthorizedDepth)
	{
		return false;
	}

	const FVector Direction = ShotDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	const double CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(OutwardHalfAngleDegrees));
	return FVector::DotProduct(Direction, OutwardNormal) >= CosHalfAngle;
}

bool UNavalFireWindowComponent::IsMuzzlePaired(
	const FVector& MuzzleLocation, const FVector& MuzzleDirection, int32 ShooterTeamId) const
{
	return IsIntact() && AllowsShot(MuzzleLocation, MuzzleDirection, ShooterTeamId);
}
