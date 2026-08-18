// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraCameraMode_TopDownFollow.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraCameraMode_TopDownFollow)

ULyraCameraMode_TopDownFollow::ULyraCameraMode_TopDownFollow()
	: PivotOffset(FVector::ZeroVector)
	, FollowRotation(-60.0f, 0.0f, 0.0f)
	, FollowDistance(1800.0f)
{
}

void ULyraCameraMode_TopDownFollow::UpdateView(float DeltaTime)
{
	const FVector PivotLocation = GetPivotLocation() + PivotOffset;
	const FRotator PivotRotation = FollowRotation;

	View.Location = PivotLocation - PivotRotation.Vector() * FollowDistance;
	View.Rotation = PivotRotation;
	View.ControlRotation = PivotRotation;
	View.FieldOfView = FieldOfView;
}
