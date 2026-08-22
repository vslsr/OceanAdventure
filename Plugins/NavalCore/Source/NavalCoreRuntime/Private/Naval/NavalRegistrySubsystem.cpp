// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalRegistrySubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
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
