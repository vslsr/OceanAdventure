// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "OceanWaterSurface.generated.h"

/** Deterministic world-space water-surface sample shared by ocean presentation and buoyant actors. */
USTRUCT(BlueprintType)
struct OCEANCORERUNTIME_API FOceanWaterSurfaceSample
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Water")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Water")
	FVector Normal = FVector::UpVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Water")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Water")
	bool bIsValid = false;
};
