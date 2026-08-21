// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/BuildStructureSubsystem.h"

#include "Building/BuildStructureComponent.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BuildStructureSubsystem)

void UBuildStructureSubsystem::RegisterStructure(UBuildStructureComponent* Structure)
{
	if (Structure)
	{
		Structures.AddUnique(Structure);
	}
}

void UBuildStructureSubsystem::UnregisterStructure(UBuildStructureComponent* Structure)
{
	Structures.RemoveAllSwap(
		[Structure](const TWeakObjectPtr<UBuildStructureComponent>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == Structure;
		},
		EAllowShrinking::No);
}

UBuildStructureComponent* UBuildStructureSubsystem::FindNearestStructure(
	const FVector& WorldLocation,
	double MaxDistance) const
{
	UBuildStructureComponent* Best = nullptr;
	double BestDistanceSquared = FMath::Square(MaxDistance);

	for (const TWeakObjectPtr<UBuildStructureComponent>& Entry : Structures)
	{
		UBuildStructureComponent* Structure = Entry.Get();
		const AActor* OwnerActor = Structure ? Structure->GetOwner() : nullptr;
		if (!OwnerActor)
		{
			continue;
		}

		const double DistanceSquared = FVector::DistSquared(WorldLocation, OwnerActor->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = Structure;
		}
	}
	return Best;
}
