// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/LyraCameraMode.h"

#include "LyraCameraMode_TopDownFollow.generated.h"

/** A fixed-angle camera whose pivot follows the current Lyra camera target. */
UCLASS(Blueprintable)
class TOPDOWNFEATURERUNTIME_API ULyraCameraMode_TopDownFollow : public ULyraCameraMode
{
	GENERATED_BODY()

public:
	ULyraCameraMode_TopDownFollow();
	virtual void OnActivation() override;

protected:
	virtual void UpdateView(float DeltaTime) override;

	/** Offset from the target pawn's standard Lyra camera pivot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Top Down Camera")
	FVector PivotOffset;

	/** Fixed camera and control rotation. Its yaw also defines Lyra's WASD movement frame. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Top Down Camera")
	FRotator FollowRotation;

	/** Distance from the pawn pivot along the inverse camera forward vector. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Top Down Camera", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FollowDistance;

	/** Higher values make wheel zoom and drag rotation settle faster. Zero snaps immediately. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Top Down Camera", meta = (ClampMin = "0.0"))
	float CameraInputInterpSpeed;

	float CurrentFollowDistance;
	float CurrentYawOffset;
};
