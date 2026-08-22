// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalBallistics.h"

#include "CollisionShape.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Naval/NavalFireWindowComponent.h"
#include "Naval/NavalPartComponent.h"
#include "Naval/NavalTeamStatics.h"

namespace FNavalBallistics
{
	static const UWorld* ResolveWorld(const UObject* WorldContextObject)
	{
		return GEngine
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr;
	}

	static UNavalFireWindowComponent* FindWindow(const FHitResult& Hit)
	{
		const AActor* HitActor = Hit.GetActor();
		return HitActor ? HitActor->FindComponentByClass<UNavalFireWindowComponent>() : nullptr;
	}

	static const UNavalPartComponent* FindPart(const FHitResult& Hit)
	{
		const AActor* HitActor = Hit.GetActor();
		return HitActor ? HitActor->FindComponentByClass<UNavalPartComponent>() : nullptr;
	}

	bool IsStructuralBlocker(const FHitResult& Hit)
	{
		const UNavalPartComponent* Part = FindPart(Hit);
		if (!Part)
		{
			return false;
		}

		const ENavalPartType PartType = Part->GetPartType();
		return PartType == ENavalPartType::Wall || PartType == ENavalPartType::FireWindow;
	}

	bool ResolveShot(const FNavalShotQuery& Query, FNavalShotResult& OutResult)
	{
		OutResult = FNavalShotResult();

		const UWorld* World = ResolveWorld(Query.WorldContextObject);
		if (!World)
		{
			return false;
		}

		const FVector Delta = Query.End - Query.Start;
		const double Distance = Delta.Size();
		if (Distance <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		// A heavy weapon refuses the shot rather than blasting its own emplacement; this is
		// what lets a light-weapon player close the distance and force the gunner off the gun.
		if (Query.MinimumRange > 0.0f && Distance < Query.MinimumRange)
		{
			OutResult.Reason = ENavalShotBlockReason::MinimumRange;
			return false;
		}

		const FVector Direction = Delta / Distance;
		const int32 TeamId = Query.TeamId != INDEX_NONE
			? Query.TeamId
			: NavalTeam::GetTeamId(Query.ShotInstigator);

		FCollisionQueryParams TraceParams(FName(TEXT("NavalResolveShot")), /*bTraceComplex=*/true);
		TraceParams.bReturnPhysicalMaterial = true;
		if (Query.ShotInstigator)
		{
			TraceParams.AddIgnoredActor(Query.ShotInstigator);
		}
		TraceParams.AddIgnoredActors(Query.IgnoreActors);

		// Multi rather than single: a friendly window is not an obstacle, so the trace has to
		// keep going past it and evaluate what stands behind it.
		TArray<FHitResult> Hits;
		if (Query.SweepRadius > 0.0f)
		{
			World->SweepMultiByChannel(
				Hits,
				Query.Start,
				Query.End,
				FQuat::Identity,
				Query.TraceChannel,
				FCollisionShape::MakeSphere(Query.SweepRadius),
				TraceParams);
		}
		else
		{
			World->LineTraceMultiByChannel(Hits, Query.Start, Query.End, Query.TraceChannel, TraceParams);
		}

		for (const FHitResult& Hit : Hits)
		{
			if (!Hit.bBlockingHit)
			{
				continue;
			}

			if (UNavalFireWindowComponent* Window = FindWindow(Hit))
			{
				const UNavalPartComponent* WindowPart = FindPart(Hit);
				if (WindowPart && !WindowPart->IsPhysicallyPresent())
				{
					// The frame was shot out. A gap is a gap for everyone, which is how
					// attackers break a window position open.
					continue;
				}

				if (Window->AllowsShot(Query.Start, Direction, TeamId))
				{
					OutResult.PassedWindow = Window;
					continue;
				}

				// Wrong team, wrong side or outside the arc: the window is just a wall now.
				OutResult.bHit = true;
				OutResult.Reason = ENavalShotBlockReason::WindowRejected;
				OutResult.Hit = Hit;
				return true;
			}

			OutResult.bHit = true;
			OutResult.Hit = Hit;
			OutResult.Reason = IsStructuralBlocker(Hit)
				? ENavalShotBlockReason::Wall
				: ENavalShotBlockReason::None;
			return true;
		}

		return false;
	}

	bool IsFireLineClear(
		const FNavalShotQuery& Query, ENavalShotBlockReason& OutReason, FVector& OutBlockLocation)
	{
		FNavalShotResult Result;
		ResolveShot(Query, Result);

		OutReason = Result.Reason;
		OutBlockLocation = Result.bHit ? Result.Hit.ImpactPoint : Query.End;

		// Only the shooter's own structure counts as "blocked" for the preview: an enemy hull
		// in the way is a target, not an obstruction.
		return Result.Reason != ENavalShotBlockReason::Wall
			&& Result.Reason != ENavalShotBlockReason::WindowRejected
			&& Result.Reason != ENavalShotBlockReason::MinimumRange;
	}

	bool IsExplosionOccluded(
		const UObject* WorldContextObject,
		const FVector& ExplosionOrigin,
		const AActor* Target,
		int32 TeamId,
		ECollisionChannel TraceChannel)
	{
		const UWorld* World = ResolveWorld(WorldContextObject);
		if (!Target || !World)
		{
			return true;
		}

		// Deliberately not ResolveShot: design 7.7 rule 6 says a one-way window passes weapon
		// projectiles only. Blast damage is stopped by the window like any other wall, so the
		// window exception must not be applied here at all.
		FCollisionQueryParams TraceParams(FName(TEXT("NavalExplosionOcclusion")), /*bTraceComplex=*/true);
		// The target must not shadow the test, or every target would occlude itself.
		TraceParams.AddIgnoredActor(Target);

		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByChannel(
			Hit, ExplosionOrigin, Target->GetActorLocation(), TraceChannel, TraceParams);

		// TeamId is unused on purpose: a wall is symmetric, so a player cannot lob a grenade
		// over their own cover either.
		(void)TeamId;
		return bBlocked;
	}
}
