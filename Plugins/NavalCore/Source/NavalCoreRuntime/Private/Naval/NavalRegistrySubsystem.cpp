// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalRegistrySubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Naval/NavalStationInterface.h"
#include "Naval/NavalVesselComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalRegistrySubsystem)

UNavalRegistrySubsystem* UNavalRegistrySubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<UNavalRegistrySubsystem>() : nullptr;
}

bool UNavalRegistrySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UNavalRegistrySubsystem::Deinitialize()
{
	Vessels.Reset();
	Stations.Reset();
	Super::Deinitialize();
}

void UNavalRegistrySubsystem::RegisterVessel(UNavalVesselComponent* Vessel)
{
	if (Vessel)
	{
		Vessels.AddUnique(Vessel);
	}
}

void UNavalRegistrySubsystem::UnregisterVessel(UNavalVesselComponent* Vessel)
{
	Vessels.RemoveAll(
		[Vessel](const TWeakObjectPtr<UNavalVesselComponent>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == Vessel;
		});
}

void UNavalRegistrySubsystem::CollectOperationalVessels(TArray<UNavalVesselComponent*>& OutVessels) const
{
	OutVessels.Reset();
	for (const TWeakObjectPtr<UNavalVesselComponent>& Entry : Vessels)
	{
		UNavalVesselComponent* Vessel = Entry.Get();
		if (Vessel && Vessel->GetVesselState() != ENavalVesselState::Wreck)
		{
			OutVessels.Add(Vessel);
		}
	}
}

void UNavalRegistrySubsystem::RegisterStation(AActor* Station)
{
	// Cast<> rather than trusting the caller: registering something that cannot answer the
	// station questions would put a null-interface entry in every query's inner loop.
	if (Station && Cast<INavalStationInterface>(Station))
	{
		Stations.AddUnique(Station);
	}
}

void UNavalRegistrySubsystem::UnregisterStation(AActor* Station)
{
	Stations.RemoveAll(
		[Station](const TWeakObjectPtr<AActor>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == Station;
		});
}

AActor* UNavalRegistrySubsystem::FindNearestStation(
	const FVector& Location, double Radius, TSubclassOf<AActor> StationClass) const
{
	// Inclusive, matching the accept checks: they reject only past the square of the range,
	// so a station sitting exactly on the boundary stays findable.
	const double RadiusSquared = FMath::Square(FMath::Max(0.0, Radius));

	AActor* Nearest = nullptr;
	double NearestDistanceSquared = TNumericLimits<double>::Max();

	for (const TWeakObjectPtr<AActor>& Entry : Stations)
	{
		AActor* Station = Entry.Get();
		const INavalStationInterface* AsStation = Cast<INavalStationInterface>(Station);
		if (!AsStation)
		{
			continue;
		}
		if (StationClass && !Station->IsA(StationClass))
		{
			continue;
		}

		const double DistanceSquared = FVector::DistSquared(Location, AsStation->GetStationWorldLocation());
		if (DistanceSquared <= RadiusSquared && DistanceSquared < NearestDistanceSquared)
		{
			NearestDistanceSquared = DistanceSquared;
			Nearest = Station;
		}
	}

	return Nearest;
}

AActor* UNavalRegistrySubsystem::FindReachableStation(
	const AActor* Candidate, TSubclassOf<AActor> StationClass, double RangeScale) const
{
	if (!Candidate)
	{
		return nullptr;
	}

	const double ClampedScale = FMath::Clamp(RangeScale, 0.0, 1.0);
	const FVector CandidateLocation = Candidate->GetActorLocation();

	AActor* Nearest = nullptr;
	double NearestDistanceSquared = TNumericLimits<double>::Max();

	for (const TWeakObjectPtr<AActor>& Entry : Stations)
	{
		AActor* Station = Entry.Get();
		const INavalStationInterface* AsStation = Cast<INavalStationInterface>(Station);
		if (!AsStation)
		{
			continue;
		}
		if (StationClass && !Station->IsA(StationClass))
		{
			continue;
		}

		// The station's own check first: it is the one the server will run, and it rejects
		// for reasons distance cannot see -- wrong team, seat taken, still under construction.
		FGameplayTag FailReason;
		if (!AsStation->CanOperateStation(Candidate, FailReason))
		{
			continue;
		}

		const double DistanceSquared =
			FVector::DistSquared(CandidateLocation, AsStation->GetStationWorldLocation());
		if (ClampedScale < 1.0)
		{
			const double ScaledRange = AsStation->GetStationInteractionRange() * ClampedScale;
			if (DistanceSquared > FMath::Square(ScaledRange))
			{
				continue;
			}
		}

		if (DistanceSquared < NearestDistanceSquared)
		{
			NearestDistanceSquared = DistanceSquared;
			Nearest = Station;
		}
	}

	return Nearest;
}

UNavalVesselComponent* UNavalRegistrySubsystem::FindNearestVessel(const FVector& Location, int32 TeamFilter) const
{
	UNavalVesselComponent* Nearest = nullptr;
	double NearestDistanceSquared = TNumericLimits<double>::Max();

	for (const TWeakObjectPtr<UNavalVesselComponent>& Entry : Vessels)
	{
		UNavalVesselComponent* Vessel = Entry.Get();
		const AActor* VesselActor = Vessel ? Vessel->GetOwner() : nullptr;
		if (!VesselActor)
		{
			continue;
		}
		if (TeamFilter != INDEX_NONE && Vessel->GetTeamId() != TeamFilter)
		{
			continue;
		}

		const double DistanceSquared = FVector::DistSquared(Location, VesselActor->GetActorLocation());
		if (DistanceSquared < NearestDistanceSquared)
		{
			NearestDistanceSquared = DistanceSquared;
			Nearest = Vessel;
		}
	}

	return Nearest;
}
