// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "NavalSpawnStatics.generated.h"

class AActor;

/**
 * Placement queries for putting an actor down at a remembered point.
 *
 * Framework layer on purpose: a reconnect anchor, a cheat teleport, a save restore and an
 * editor tool all need the same "is there room here, and where is the floor" answer, and
 * none of them goes through GAS.
 */
UCLASS()
class NAVALCORERUNTIME_API UNavalSpawnStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * A spot near Target that Actor's own collision fits in, standing on whatever is below.
	 *
	 * Two things are being solved at once. Remembered points are rarely at floor height --
	 * a heavy weapon's operator point sits about a metre above the deck -- so a candidate is
	 * dropped onto the surface under it before it is accepted, otherwise the actor is placed
	 * in the air and falls. And the exact point is often taken by the time it is used, so
	 * candidates spiral outwards and the first clear one wins.
	 *
	 * Returns false when nothing within SearchRadius is clear; callers are expected to keep
	 * the actor where it already is rather than force it in.
	 */
	UFUNCTION(BlueprintCallable, Category = "Naval|Spawn")
	static bool FindClearSpotNear(
		AActor* Actor,
		const FVector& Target,
		float SearchRadius,
		FVector& OutLocation);
};
