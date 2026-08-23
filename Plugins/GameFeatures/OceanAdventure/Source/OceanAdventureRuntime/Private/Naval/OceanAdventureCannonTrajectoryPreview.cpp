// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureCannonTrajectoryPreview.h"

#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureCannonTrajectoryPreview)

AOceanAdventureCannonTrajectoryPreview::AOceanAdventureCannonTrajectoryPreview()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("TrajectorySpline"));
	SetRootComponent(Spline);
	Spline->SetClosedLoop(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SegmentMeshFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	SegmentMesh = SegmentMeshFinder.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ClearMaterialFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	ClearMaterial = ClearMaterialFinder.Object;
	BlockedMaterial = ClearMaterial;
}

USplineMeshComponent* AOceanAdventureCannonTrajectoryPreview::GetOrCreateSegment(int32 Index)
{
	while (Segments.Num() <= Index)
	{
		USplineMeshComponent* Segment = NewObject<USplineMeshComponent>(this);
		Segment->SetupAttachment(Spline);
		Segment->SetMobility(EComponentMobility::Movable);
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetGenerateOverlapEvents(false);
		Segment->SetCastShadow(false);
		Segment->SetStaticMesh(SegmentMesh);
		Segment->SetForwardAxis(ESplineMeshAxis::X, false);
		Segment->RegisterComponent();
		Segments.Add(Segment);
	}
	return Segments[Index];
}

void AOceanAdventureCannonTrajectoryPreview::SetTrajectory(
	const TArray<FVector>& WorldPoints, bool bBlocked)
{
	if (!Spline || WorldPoints.Num() < 2)
	{
		HideTrajectory();
		return;
	}

	Spline->SetSplinePoints(WorldPoints, ESplineCoordinateSpace::World, true);
	const int32 RequiredSegments = WorldPoints.Num() - 1;
	for (int32 Index = 0; Index < RequiredSegments; ++Index)
	{
		USplineMeshComponent* Segment = GetOrCreateSegment(Index);
		const FVector Start = Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local);
		const FVector End = Spline->GetLocationAtSplinePoint(Index + 1, ESplineCoordinateSpace::Local);
		const FVector Tangent = (End - Start);
		Segment->SetStartAndEnd(Start, Tangent, End, Tangent, false);
		Segment->SetStartScale(FVector2D(0.025f, 0.025f), false);
		Segment->SetEndScale(FVector2D(0.025f, 0.025f), false);
		Segment->SetMaterial(0, bBlocked ? BlockedMaterial : ClearMaterial);
		Segment->SetVisibility(true, true);
		Segment->UpdateMesh();
	}

	for (int32 Index = RequiredSegments; Index < Segments.Num(); ++Index)
	{
		Segments[Index]->SetVisibility(false, true);
	}
}

void AOceanAdventureCannonTrajectoryPreview::HideTrajectory()
{
	for (USplineMeshComponent* Segment : Segments)
	{
		if (Segment)
		{
			Segment->SetVisibility(false, true);
		}
	}
}
