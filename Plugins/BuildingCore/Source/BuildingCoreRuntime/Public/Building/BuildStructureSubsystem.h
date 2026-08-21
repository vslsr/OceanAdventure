// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#include "BuildStructureSubsystem.generated.h"

class UBuildStructureComponent;

/**
 * Registry of every live build structure in the world.
 *
 * Callers that need "the structure near me" used to iterate every UObject of the class each
 * frame; structures register themselves here instead so lookups only touch the handful of
 * hosts that actually exist.
 */
UCLASS()
class BUILDINGCORERUNTIME_API UBuildStructureSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterStructure(UBuildStructureComponent* Structure);
	void UnregisterStructure(UBuildStructureComponent* Structure);

	const TArray<TWeakObjectPtr<UBuildStructureComponent>>& GetStructures() const { return Structures; }

	/** Nearest registered structure within MaxDistance of WorldLocation, or null. */
	UBuildStructureComponent* FindNearestStructure(const FVector& WorldLocation, double MaxDistance) const;

private:
	TArray<TWeakObjectPtr<UBuildStructureComponent>> Structures;
};
