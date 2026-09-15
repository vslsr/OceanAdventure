// Copyright Epic Games, Inc. All Rights Reserved.

#include "Environment/LineArtEnvironmentSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Environment/LineArtCoreSettings.h"
#include "Environment/LineArtPointLightComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "LineArtCoreRuntimeModule.h"
#include "LineArtParameterNames.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

namespace
{
	/**
	 * GLSL smoothstep, including the reversed-edge case.
	 *
	 * Written out rather than routed through FMath: the ink lift runs from 0.55 DOWN to
	 * 0.12, and a helper that assumes Edge0 < Edge1 silently returns 0 for that range --
	 * which reads as "the night lift never happens", the exact bug this code exists to
	 * prevent.
	 */
	float SmoothStepGLSL(float Edge0, float Edge1, float Value)
	{
		if (FMath::IsNearlyEqual(Edge0, Edge1))
		{
			return Value < Edge0 ? 0.0f : 1.0f;
		}

		const float T = FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}
}

ULineArtEnvironmentSubsystem* ULineArtEnvironmentSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<ULineArtEnvironmentSubsystem>() : nullptr;
}

void ULineArtEnvironmentSubsystem::Initialize(FSubsystemCollectionBase& InCollection)
{
	Super::Initialize(InCollection);

	const ULineArtCoreSettings& Settings = ULineArtCoreSettings::Get();
	Environment = Settings.DefaultEnvironment;

	if (Settings.EnvironmentCollection.IsNull())
	{
		UE_LOG(LogLineArtCore, Warning,
			TEXT("No EnvironmentCollection configured. Set it under Project Settings > Game > Line Art Core, ")
			TEXT("or run CreateLineArtCoreAssets.py to author it."));
		return;
	}

	Collection = Settings.EnvironmentCollection.LoadSynchronous();
	if (!Collection)
	{
		UE_LOG(LogLineArtCore, Error, TEXT("Failed to load EnvironmentCollection %s."),
			*Settings.EnvironmentCollection.ToString());
	}
}

void ULineArtEnvironmentSubsystem::Deinitialize()
{
	PointLights.Reset();
	ReportedMissingParameters.Reset();
	Collection = nullptr;

	Super::Deinitialize();
}

TStatId ULineArtEnvironmentSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULineArtEnvironmentSubsystem, STATGROUP_Tickables);
}

void ULineArtEnvironmentSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		// A dedicated server draws nothing, so every write here would be pure cost.
		return;
	}

	WriteCollection();
}

void ULineArtEnvironmentSubsystem::SetEnvironment(const FLineArtEnvironmentState& NewEnvironment)
{
	Environment = NewEnvironment;
	Environment.SunDirection = Environment.SunDirection.GetSafeNormal();
}

void ULineArtEnvironmentSubsystem::RegisterPointLight(ULineArtPointLightComponent* PointLight)
{
	if (!PointLight)
	{
		return;
	}

	PointLights.AddUnique(PointLight);
}

void ULineArtEnvironmentSubsystem::UnregisterPointLight(ULineArtPointLightComponent* PointLight)
{
	PointLights.RemoveAll([PointLight](const TWeakObjectPtr<ULineArtPointLightComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == PointLight;
	});
}

int32 ULineArtEnvironmentSubsystem::GetRegisteredPointLightCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ULineArtPointLightComponent>& Entry : PointLights)
	{
		Count += Entry.IsValid() ? 1 : 0;
	}
	return Count;
}

void ULineArtEnvironmentSubsystem::WriteCollection()
{
	UWorld* World = GetWorld();
	if (!World || !Collection)
	{
		return;
	}

	UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection);
	if (!Instance)
	{
		return;
	}

	const ULineArtCoreSettings& Settings = ULineArtCoreSettings::Get();

	// The ink lift. Fill colours are dimmed by the ambient light but outline lines are not
	// lit at all, so without this the paper sinks past the ink at dusk and every silhouette
	// disappears at once.
	const float Lift = SmoothStepGLSL(Settings.InkLiftFrom, Settings.InkLiftTo, Environment.Daylight);

	const FLinearColor Ink =
		FMath::Lerp(Settings.BaseInkColor, Settings.NightInkColor, Lift) * Environment.InkTint;
	const FLinearColor Grid =
		Settings.BaseGridColor * (1.0f - (1.0f - Settings.NightGridScale) * Lift) * Environment.InkTint;

	SetVector(*Instance, LineArtParameterNames::InkColor, Ink);
	SetVector(*Instance, LineArtParameterNames::GridColor, Grid);
	SetScalar(*Instance, LineArtParameterNames::GridOpacity,
		Settings.GridBaseOpacity * (1.0f - Settings.GridNightOpacityScale * Lift));

	SetScalar(*Instance, LineArtParameterNames::Daylight, Environment.Daylight);
	SetScalar(*Instance, LineArtParameterNames::OutlineThickness, Environment.OutlineThickness);
	SetScalar(*Instance, LineArtParameterNames::FogNear, Environment.FogNear);
	SetScalar(*Instance, LineArtParameterNames::FogFar, Environment.FogFar);
	SetScalar(*Instance, LineArtParameterNames::ScatterStrength, Environment.ScatterStrength);
	SetScalar(*Instance, LineArtParameterNames::CloudShadowStrength, Environment.CloudShadowStrength);

	SetVector(*Instance, LineArtParameterNames::AmbientColor, Environment.AmbientColor);
	SetVector(*Instance, LineArtParameterNames::SkyTint, Environment.SkyTint);
	SetVector(*Instance, LineArtParameterNames::BounceTint, Environment.BounceTint);
	SetVector(*Instance, LineArtParameterNames::ScatterColor, Environment.ScatterColor);
	SetVector(*Instance, LineArtParameterNames::FogColor, Environment.FogColor);

	const FVector Sun = Environment.SunDirection.GetSafeNormal();
	SetVector(*Instance, LineArtParameterNames::SunDirection,
		FLinearColor(static_cast<float>(Sun.X), static_cast<float>(Sun.Y), static_cast<float>(Sun.Z), 0.0f));
	SetVector(*Instance, LineArtParameterNames::CloudShadowOffset,
		FLinearColor(static_cast<float>(Environment.CloudShadowOffset.X),
			static_cast<float>(Environment.CloudShadowOffset.Y), 0.0f, 0.0f));

	WritePointLights(*Instance);
}

void ULineArtEnvironmentSubsystem::WritePointLights(UMaterialParameterCollectionInstance& Instance)
{
	const UWorld* World = GetWorld();
	const float TimeSeconds = World ? World->GetTimeSeconds() : 0.0f;
	const FVector ViewLocation = GetViewLocation();

	struct FCandidate
	{
		const ULineArtPointLightComponent* Light = nullptr;
		FVector Location = FVector::ZeroVector;
		float Intensity = 0.0f;
		double DistanceSquared = 0.0;
	};

	TArray<FCandidate, TInlineAllocator<16>> Candidates;
	for (int32 Index = PointLights.Num() - 1; Index >= 0; --Index)
	{
		const ULineArtPointLightComponent* Light = PointLights[Index].Get();
		if (!Light)
		{
			PointLights.RemoveAtSwap(Index);
			continue;
		}

		const float Intensity = Light->GetEffectiveIntensity(Environment.Daylight, TimeSeconds);
		if (Intensity <= 0.002f || Light->Radius <= 0.0f)
		{
			continue;
		}

		const FVector Location = Light->GetComponentLocation();
		const double DistanceSquared = FVector::DistSquared(Location, ViewLocation);

		// Out of reach of the view by more than its own radius: it cannot brighten anything
		// the camera can see, so it must not take one of the fixed slots.
		if (DistanceSquared > FMath::Square(static_cast<double>(Light->Radius)) * 4.0)
		{
			continue;
		}

		Candidates.Add(FCandidate{Light, Location, Intensity, DistanceSquared});
	}

	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		return A.DistanceSquared < B.DistanceSquared;
	});

	for (int32 Slot = 0; Slot < LINE_ART_MAX_POINT_LIGHTS; ++Slot)
	{
		if (Candidates.IsValidIndex(Slot))
		{
			const FCandidate& Candidate = Candidates[Slot];
			SetVector(Instance, LineArtParameterNames::PointLightPosition(Slot),
				FLinearColor(static_cast<float>(Candidate.Location.X), static_cast<float>(Candidate.Location.Y),
					static_cast<float>(Candidate.Location.Z), Candidate.Light->Radius));

			FLinearColor NearColor = Candidate.Light->Color;
			NearColor.A = Candidate.Intensity;
			SetVector(Instance, LineArtParameterNames::PointLightColor(Slot), NearColor);
			SetVector(Instance, LineArtParameterNames::PointLightEdgeColor(Slot), Candidate.Light->EdgeColor);
		}
		else
		{
			// Empty slots are written every frame rather than left stale: the shader reads
			// intensity 0 as "skip", and a radius of 1 keeps the division well defined even
			// though that branch is never taken.
			SetVector(Instance, LineArtParameterNames::PointLightPosition(Slot), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
			SetVector(Instance, LineArtParameterNames::PointLightColor(Slot), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
			SetVector(Instance, LineArtParameterNames::PointLightEdgeColor(Slot), FLinearColor::Black);
		}
	}
}

FVector ULineArtEnvironmentSubsystem::GetViewLocation() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

	for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		const APlayerController* Controller = Iterator->Get();
		if (!Controller || !Controller->IsLocalController())
		{
			continue;
		}

		if (const APlayerCameraManager* CameraManager = Controller->PlayerCameraManager)
		{
			return CameraManager->GetCameraLocation();
		}

		if (const APawn* Pawn = Controller->GetPawn())
		{
			return Pawn->GetActorLocation();
		}
	}

	return FVector::ZeroVector;
}

void ULineArtEnvironmentSubsystem::SetScalar(UMaterialParameterCollectionInstance& Instance, FName Name, float Value)
{
	if (!Instance.SetScalarParameterValue(Name, Value) && !ReportedMissingParameters.Contains(Name))
	{
		ReportedMissingParameters.Add(Name);
		UE_LOG(LogLineArtCore, Error,
			TEXT("Scalar parameter '%s' is missing from the line-art collection; that value will never update. ")
			TEXT("Re-run CreateLineArtCoreAssets.py or fix the name in LineArtParameterNames.h."), *Name.ToString());
	}
}

void ULineArtEnvironmentSubsystem::SetVector(UMaterialParameterCollectionInstance& Instance, FName Name, const FLinearColor& Value)
{
	if (!Instance.SetVectorParameterValue(Name, Value) && !ReportedMissingParameters.Contains(Name))
	{
		ReportedMissingParameters.Add(Name);
		UE_LOG(LogLineArtCore, Error,
			TEXT("Vector parameter '%s' is missing from the line-art collection; that value will never update. ")
			TEXT("Re-run CreateLineArtCoreAssets.py or fix the name in LineArtParameterNames.h."), *Name.ToString());
	}
}
