// Copyright Epic Games, Inc. All Rights Reserved.

#include "Preview/LineArtPreviewActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Environment/LineArtCoreSettings.h"
#include "Environment/LineArtPointLightComponent.h"
#include "LineArtCoreRuntimeModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Parameter on M_LineArt_Fill. Mirrors the name authored by CreateLineArtCoreAssets.py. */
	const FName FillBaseColorParameter(TEXT("BaseColor"));

	UMaterialInterface* LoadConfigured(const TSoftObjectPtr<UMaterialInterface>& Reference, const TCHAR* Label)
	{
		if (Reference.IsNull())
		{
			UE_LOG(LogLineArtCore, Warning,
				TEXT("No %s configured. Set it under Project Settings > Game > Line Art Core, ")
				TEXT("or run CreateLineArtCoreAssets.py to author the master materials."), Label);
			return nullptr;
		}

		UMaterialInterface* Material = Reference.LoadSynchronous();
		if (!Material)
		{
			UE_LOG(LogLineArtCore, Error, TEXT("Failed to load %s %s."), Label, *Reference.ToString());
		}
		return Material;
	}
}

ALineArtPreviewActor::ALineArtPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;

	FillMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FillMesh"));
	SetRootComponent(FillMesh);

	OutlineMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OutlineMesh"));
	OutlineMesh->SetupAttachment(FillMesh);
	// The hull is pure silhouette: it must not be hit by traces, and it must not cast a
	// second shadow of a shape that is already there.
	OutlineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OutlineMesh->SetCastShadow(false);

	PointLight = CreateDefaultSubobject<ULineArtPointLightComponent>(TEXT("PointLight"));
	PointLight->SetupAttachment(FillMesh);
	PointLight->SetRelativeLocation(FVector(120.0, 0.0, 60.0));
	// A production campfire fades out as the sun rises. A preview light that is invisible
	// at the default noon daylight would just read as "the feature does not work".
	PointLight->bFadeInDaylight = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		// A sphere, not a cube: smooth normals are what the inverted hull needs, and the
		// first thing anyone drops should show the style working rather than the failure.
		PreviewMesh = SphereFinder.Object;
	}
}

void ALineArtPreviewActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	RebuildPreview();
}

#if WITH_EDITOR
void ALineArtPreviewActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	RebuildPreview();
}
#endif

void ALineArtPreviewActor::RebuildPreview()
{
	if (!FillMesh || !OutlineMesh)
	{
		return;
	}

	FillMesh->SetStaticMesh(PreviewMesh);
	OutlineMesh->SetStaticMesh(PreviewMesh);

	const ULineArtCoreSettings& Settings = ULineArtCoreSettings::Get();

	if (UMaterialInterface* Fill = LoadConfigured(Settings.FillMaterial, TEXT("FillMaterial")))
	{
		// Rebuilt rather than reused: the configured material can change under us in the
		// editor, and a dynamic instance parented to the old one keeps the old look with no
		// visible reason why.
		FillMaterialInstance = UMaterialInstanceDynamic::Create(Fill, this);
		if (FillMaterialInstance)
		{
			FillMaterialInstance->SetVectorParameterValue(FillBaseColorParameter, FillColor);
			FillMesh->SetMaterial(0, FillMaterialInstance);
		}
	}

	if (UMaterialInterface* Outline = LoadConfigured(Settings.OutlineMaterial, TEXT("OutlineMaterial")))
	{
		// No dynamic instance: the ink colour is scene-wide state written into the parameter
		// collection, not something one actor gets to disagree about.
		OutlineMesh->SetMaterial(0, Outline);
	}

	OutlineMesh->SetVisibility(bShowOutline);

	if (PointLight)
	{
		PointLight->SetVisibility(bShowPointLight);
		PointLight->Intensity = bShowPointLight ? 1.0f : 0.0f;
	}
}
