// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraCameraMode_TopDownFollow.h"

#include "TopDownPawnComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraCameraMode_TopDownFollow)

ULyraCameraMode_TopDownFollow::ULyraCameraMode_TopDownFollow()
	: PivotOffset(FVector::ZeroVector)
	, FollowRotation(-60.0f, 0.0f, 0.0f)
	, FollowDistance(1800.0f)
	, CameraInputInterpSpeed(12.0f)
	, CurrentFollowDistance(FollowDistance)
	, CurrentYawOffset(0.0f)
{
}

void ULyraCameraMode_TopDownFollow::OnActivation()
{
	Super::OnActivation();
	CurrentFollowDistance = FollowDistance;
	CurrentYawOffset = 0.0f;
}

void ULyraCameraMode_TopDownFollow::UpdateView(float DeltaTime)
{
	const FVector PivotLocation = GetPivotLocation() + PivotOffset;
	float DesiredDistance = FollowDistance;
	float DesiredYawOffset = 0.0f;
	if (const AActor* TargetActor = GetTargetActor())
	{
		if (const UTopDownPawnComponent* TopDownComponent = TargetActor->FindComponentByClass<UTopDownPawnComponent>())
		{
			DesiredDistance = TopDownComponent->GetCameraDistance();
			DesiredYawOffset = TopDownComponent->GetCameraYawOffset();
		}
	}

	if (CameraInputInterpSpeed > 0.0f)
	{
		CurrentFollowDistance = FMath::FInterpTo(CurrentFollowDistance, DesiredDistance, DeltaTime, CameraInputInterpSpeed);
		const float YawDelta = FRotator::NormalizeAxis(DesiredYawOffset - CurrentYawOffset);
		CurrentYawOffset = FRotator::NormalizeAxis(
			CurrentYawOffset + YawDelta * FMath::Clamp(CameraInputInterpSpeed * DeltaTime, 0.0f, 1.0f));
	}
	else
	{
		CurrentFollowDistance = DesiredDistance;
		CurrentYawOffset = DesiredYawOffset;
	}

	FRotator PivotRotation = FollowRotation;
	PivotRotation.Yaw = FRotator::NormalizeAxis(PivotRotation.Yaw + CurrentYawOffset);

	View.Location = PivotLocation - PivotRotation.Vector() * CurrentFollowDistance;
	View.Rotation = PivotRotation;
	View.ControlRotation = PivotRotation;
	View.FieldOfView = FieldOfView;
}
