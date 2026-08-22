// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"

#include "NavalFireWindowComponent.generated.h"

class UNavalPartComponent;

/**
 * 单向射击窗 -- the only exception to "walls stop everything".
 *
 * The permission is deterministic and has nothing to do with collision: a window never turns
 * its collision off, because then enemy fire would come straight back through the open
 * shutter. Instead a shot is let through only when the shooter is on the window's team, the
 * shot starts inside the authorised depth behind it, and it leaves within the outward arc.
 * Everything else -- enemy fire, friendly fire from outside, explosions -- hits the window
 * itself, which is exactly how an attacker breaks the position: concentrate on the window,
 * then shoot through the hole.
 */
UCLASS(BlueprintType, ClassGroup = (Naval), meta = (BlueprintSpawnableComponent))
class NAVALCORERUNTIME_API UNavalFireWindowComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNavalFireWindowComponent();

	void Configure(float InOutwardHalfAngleDegrees, float InInnerAuthorizedDepth, float InOpenWindupSeconds);

	/** Team, side and arc, in that order. Any failure means the window blocks the shot. */
	UFUNCTION(BlueprintCallable, Category = "Naval|Window")
	bool AllowsShot(const FVector& ShotStart, const FVector& ShotDirection, int32 ShooterTeamId) const;

	/** Ownership follows the vessel, so a captured raft's windows change hands with it. */
	UFUNCTION(BlueprintPure, Category = "Naval|Window")
	int32 GetOwningTeamId() const;

	/** Outward direction the window fires through; the owning Actor's forward axis. */
	UFUNCTION(BlueprintPure, Category = "Naval|Window")
	FVector GetOutwardNormal() const;

	UFUNCTION(BlueprintPure, Category = "Naval|Window")
	FVector GetWindowLocation() const;

	/** True while the frame is intact enough to block anything at all. */
	UFUNCTION(BlueprintPure, Category = "Naval|Window")
	bool IsIntact() const;

	UFUNCTION(BlueprintPure, Category = "Naval|Window")
	float GetOpenWindupSeconds() const { return OpenWindupSeconds; }

	UFUNCTION(BlueprintPure, Category = "Naval|Window")
	float GetOutwardHalfAngleDegrees() const { return OutwardHalfAngleDegrees; }

	UFUNCTION(BlueprintPure, Category = "Naval|Window")
	float GetInnerAuthorizedDepth() const { return InnerAuthorizedDepth; }

	/** True when a muzzle at this location and heading would be paired with the window. */
	UFUNCTION(BlueprintCallable, Category = "Naval|Window")
	bool IsMuzzlePaired(const FVector& MuzzleLocation, const FVector& MuzzleDirection, int32 ShooterTeamId) const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Window",
		meta = (ClampMin = "10.0", ClampMax = "60.0", Units = "deg"))
	float OutwardHalfAngleDegrees = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Window",
		meta = (ClampMin = "10.0", Units = "cm"))
	float InnerAuthorizedDepth = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Window",
		meta = (ClampMin = "0.0", Units = "s"))
	float OpenWindupSeconds = 0.2f;
};
