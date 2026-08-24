// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalSpawnStatics.h"

#include "Components/CapsuleComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalSpawnStatics)

namespace
{
	/** How far below a candidate the floor is looked for. A deck, not a seabed. */
	constexpr float NavalSpawnGroundProbe = 400.0f;

	/** Rings tried outwards from the exact point, and samples per ring. */
	constexpr int32 NavalSpawnRingCount = 3;
	constexpr int32 NavalSpawnSamplesPerRing = 8;
}

bool UNavalSpawnStatics::FindClearSpotNear(
	AActor* Actor, const FVector& Target, float SearchRadius, FVector& OutLocation)
{
	UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}

	// The actor's own body, so the test answers "does this actor fit" rather than "does a
	// nominal capsule fit".
	float Radius = 34.0f;
	float HalfHeight = 88.0f;
	if (const ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			Radius = Capsule->GetScaledCapsuleRadius();
			HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
	}
	else
	{
		Actor->GetSimpleCollisionCylinder(Radius, HalfHeight);
	}

	const FCollisionShape Body = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	FCollisionQueryParams QueryParams(FName(TEXT("NavalFindClearSpot")), /*bTraceComplex=*/false, Actor);

	const auto TryCandidate = [&](const FVector& Candidate, FVector& OutResolved) -> bool
	{
		FVector Resolved = Candidate;

		FHitResult FloorHit;
		const FVector ProbeStart = Candidate + FVector(0.0, 0.0, static_cast<double>(HalfHeight));
		const FVector ProbeEnd = Candidate - FVector(0.0, 0.0, static_cast<double>(NavalSpawnGroundProbe));
		if (World->SweepSingleByChannel(
				FloorHit, ProbeStart, ProbeEnd, FQuat::Identity, ECC_Pawn, Body, QueryParams)
			&& !FloorHit.bStartPenetrating)
		{
			Resolved = FloorHit.Location;
		}

		if (World->OverlapBlockingTestByChannel(Resolved, FQuat::Identity, ECC_Pawn, Body, QueryParams))
		{
			return false;
		}

		OutResolved = Resolved;
		return true;
	};

	if (TryCandidate(Target, OutLocation))
	{
		return true;
	}

	const float MaxRing = FMath::Max(SearchRadius, Radius * 2.0f);
	for (int32 Ring = 1; Ring <= NavalSpawnRingCount; ++Ring)
	{
		const float Distance = MaxRing * (static_cast<float>(Ring) / static_cast<float>(NavalSpawnRingCount));
		// Each ring is rotated half a step off the previous one so the samples interleave
		// instead of lining up along the same spokes.
		const float AngleOffset = (Ring % 2 == 0) ? (180.0f / NavalSpawnSamplesPerRing) : 0.0f;
		for (int32 Sample = 0; Sample < NavalSpawnSamplesPerRing; ++Sample)
		{
			const float AngleDegrees =
				AngleOffset + (360.0f / NavalSpawnSamplesPerRing) * static_cast<float>(Sample);
			const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
			const FVector Candidate =
				Target + FVector(FMath::Cos(AngleRadians) * Distance, FMath::Sin(AngleRadians) * Distance, 0.0);
			if (TryCandidate(Candidate, OutLocation))
			{
				return true;
			}
		}
	}

	return false;
}
