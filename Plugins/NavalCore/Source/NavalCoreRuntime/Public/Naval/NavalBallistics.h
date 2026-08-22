// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "Naval/NavalCoreTypes.h"

class AActor;
class UNavalFireWindowComponent;
class UObject;

/** One shot's request. Everything the wall rule needs and nothing it does not. */
struct NAVALCORERUNTIME_API FNavalShotQuery
{
	const UObject* WorldContextObject = nullptr;
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;

	/** Whoever fired. Used to resolve the team when TeamId is left unset, and always ignored. */
	AActor* ShotInstigator = nullptr;

	/** Leave as INDEX_NONE to resolve from ShotInstigator. */
	int32 TeamId = INDEX_NONE;

	/** Non-zero turns the line trace into a sweep, matching Lyra's ranged weapon fallback. */
	float SweepRadius = 0.0f;

	/** Heavy weapons refuse to fire inside this distance instead of hitting their own feet. */
	float MinimumRange = 0.0f;

	ECollisionChannel TraceChannel = ECC_Visibility;

	TArray<AActor*> IgnoreActors;
};

/** Where the shot actually stopped, and why. */
struct NAVALCORERUNTIME_API FNavalShotResult
{
	/** True when something was hit; Hit carries it. */
	bool bHit = false;

	/**
	 * Why the shot stopped short of its aim point. None covers both "hit what it was aimed
	 * at" and "hit nothing"; the caller distinguishes those with bHit.
	 */
	ENavalShotBlockReason Reason = ENavalShotBlockReason::None;

	FHitResult Hit;

	/** Set when the shot legally passed through a friendly one-way window on the way out. */
	UNavalFireWindowComponent* PassedWindow = nullptr;
};

/**
 * The project's single ballistic authority.
 *
 * Design 7.7 states the rule as: except through a legal friendly one-way window, no ordinary
 * weapon reaches past a wall, and that is symmetric -- your own wall stops your own shot.
 * Every damage path has to run through here, character weapons and ship guns alike, or the
 * rule becomes "mostly true", which is worse than not having it: players learn a rule by
 * never seeing it broken.
 *
 * It is a pure query. It applies no damage, spawns nothing and holds no state, so the client
 * can run exactly the same call for its aim preview that the server runs for the verdict.
 */
namespace FNavalBallistics
{
	/** Traces the shot and applies the wall / one-way-window rule to the hits it finds. */
	NAVALCORERUNTIME_API bool ResolveShot(const FNavalShotQuery& Query, FNavalShotResult& OutResult);

	/**
	 * Aim preview for muzzle blocking: true when the shot would leave without a wall of the
	 * shooter's own eating it. Drives the red blocked line and the green outward cone.
	 */
	NAVALCORERUNTIME_API bool IsFireLineClear(
		const FNavalShotQuery& Query, ENavalShotBlockReason& OutReason, FVector& OutBlockLocation);

	/**
	 * Per-target occlusion for explosions. Design 7.7 rule 3: a wall between the blast and a
	 * target means zero damage, with no splash sneaking around cover.
	 */
	NAVALCORERUNTIME_API bool IsExplosionOccluded(
		const UObject* WorldContextObject,
		const FVector& ExplosionOrigin,
		const AActor* Target,
		int32 TeamId,
		ECollisionChannel TraceChannel = ECC_Visibility);

	/** True when this hit is a naval wall or window body rather than a target. */
	NAVALCORERUNTIME_API bool IsStructuralBlocker(const FHitResult& Hit);
}
