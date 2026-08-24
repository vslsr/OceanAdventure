// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureNavalStatics.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Naval/NavalHelmComponent.h"
#include "Naval/NavalLoadComponent.h"
#include "Naval/NavalMovementComponent.h"
#include "Naval/NavalPartComponent.h"
#include "Naval/NavalRegistrySubsystem.h"
#include "Naval/NavalVesselComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureNavalStatics)

AActor* UOceanAdventureNavalStatics::FindNearestStationActor(
	const UObject* WorldContextObject,
	const FVector& Origin,
	float Radius,
	TSubclassOf<AActor> StationClass)
{
	if (!StationClass)
	{
		return nullptr;
	}

	UNavalRegistrySubsystem* Registry = UNavalRegistrySubsystem::Get(WorldContextObject);
	return Registry ? Registry->FindNearestStation(Origin, Radius, StationClass) : nullptr;
}

UNavalVesselComponent* UOceanAdventureNavalStatics::FindVesselForActor(AActor* Actor)
{
	return UNavalVesselComponent::FindVessel(Actor);
}

UNavalVesselComponent* UOceanAdventureNavalStatics::FindVesselUnderPawn(APawn* Pawn)
{
	if (!Pawn)
	{
		return nullptr;
	}

	// Standing on a deck means the deck is the character's movement base, so no trace is
	// needed to answer "which ship am I on".
	if (const UPrimitiveComponent* MovementBase = Pawn->GetMovementBase())
	{
		if (UNavalVesselComponent* Vessel = UNavalVesselComponent::FindVessel(MovementBase->GetOwner()))
		{
			return Vessel;
		}
	}

	return UNavalVesselComponent::FindVessel(Pawn->GetAttachParentActor());
}

FOceanAdventureNavalHudState UOceanAdventureNavalStatics::GetVesselHudState(AActor* VesselActor)
{
	FOceanAdventureNavalHudState State;

	const UNavalVesselComponent* Vessel = VesselActor
		? VesselActor->FindComponentByClass<UNavalVesselComponent>()
		: nullptr;
	if (!Vessel)
	{
		return State;
	}

	State.bValid = true;
	State.VesselState = Vessel->GetVesselState();
	State.HullFraction = Vessel->GetHullFraction();
	State.FounderSecondsRemaining = Vessel->GetFounderSecondsRemaining();
	State.bEmergencyRepairAvailable = Vessel->IsEmergencyRepairAvailable();
	State.TeamId = Vessel->GetTeamId();
	State.RudderCapability = Vessel->GetPartCapability(ENavalPartType::Rudder);
	State.PropulsionCapability = Vessel->GetPartCapability(ENavalPartType::Propulsion);

	if (const UNavalHelmComponent* Helm = VesselActor->FindComponentByClass<UNavalHelmComponent>())
	{
		State.HelmState = Helm->GetHelmState();
		State.CaptureProgress = Helm->GetCaptureProgress();
		if (const UNavalPartComponent* Core = Helm->GetCorePart())
		{
			State.HelmCoreFraction = Core->GetDurabilityFraction();
		}
	}

	if (const UNavalLoadComponent* Load = VesselActor->FindComponentByClass<UNavalLoadComponent>())
	{
		State.LoadState = Load->GetLoadState();
	}

	if (const UNavalMovementComponent* Movement = VesselActor->FindComponentByClass<UNavalMovementComponent>())
	{
		State.SpeedFraction = Movement->GetSpeedFraction();
	}

	return State;
}

bool UOceanAdventureNavalStatics::GetCursorAimLocation(
	APlayerController* PlayerController, FVector& OutAimLocation)
{
	if (!PlayerController)
	{
		return false;
	}

	// Always project onto the operator's horizontal plane. GetHitResultUnderCursor is wrong
	// for a station: the cursor commonly hits the attached character or the cannon itself,
	// which makes the turret turn back toward its operator and appear to ignore the mouse.
	FVector WorldOrigin = FVector::ZeroVector;
	FVector WorldDirection = FVector::ForwardVector;
	const APawn* Pawn = PlayerController->GetPawn();
	if (!Pawn || !PlayerController->DeprojectMousePositionToWorld(WorldOrigin, WorldDirection))
	{
		return false;
	}

	const AActor* AttachedStation = Pawn->GetAttachParentActor();
	const double PlaneZ = AttachedStation
		? AttachedStation->GetActorLocation().Z
		: Pawn->GetActorLocation().Z;
	if (FMath::IsNearlyZero(WorldDirection.Z))
	{
		return false;
	}

	const double Distance = (PlaneZ - WorldOrigin.Z) / WorldDirection.Z;
	if (Distance <= 0.0)
	{
		return false;
	}

	OutAimLocation = WorldOrigin + WorldDirection * Distance;
	return true;
}
