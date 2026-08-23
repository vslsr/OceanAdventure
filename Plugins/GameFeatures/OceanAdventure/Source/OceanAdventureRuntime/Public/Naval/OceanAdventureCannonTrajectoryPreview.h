// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"

#include "OceanAdventureCannonTrajectoryPreview.generated.h"

class USplineComponent;
class USplineMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/** Local-only reusable spline ribbon shown while a cannon shot is charging. */
UCLASS(NotBlueprintable)
class OCEANADVENTURERUNTIME_API AOceanAdventureCannonTrajectoryPreview : public AActor
{
	GENERATED_BODY()

public:
	AOceanAdventureCannonTrajectoryPreview();

	void SetTrajectory(const TArray<FVector>& WorldPoints, bool bBlocked);
	void HideTrajectory();

private:
	USplineMeshComponent* GetOrCreateSegment(int32 Index);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USplineComponent> Spline;

	UPROPERTY()
	TArray<TObjectPtr<USplineMeshComponent>> Segments;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SegmentMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ClearMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BlockedMaterial;
};
