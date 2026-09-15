// Copyright Epic Games, Inc. All Rights Reserved.

#include "Environment/LineArtPointLightComponent.h"

#include "Environment/LineArtEnvironmentSubsystem.h"

ULineArtPointLightComponent::ULineArtPointLightComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void ULineArtPointLightComponent::OnRegister()
{
	Super::OnRegister();

	if (ULineArtEnvironmentSubsystem* Subsystem = ULineArtEnvironmentSubsystem::Get(this))
	{
		Subsystem->RegisterPointLight(this);
	}
}

void ULineArtPointLightComponent::OnUnregister()
{
	if (ULineArtEnvironmentSubsystem* Subsystem = ULineArtEnvironmentSubsystem::Get(this))
	{
		Subsystem->UnregisterPointLight(this);
	}

	Super::OnUnregister();
}

float ULineArtPointLightComponent::GetEffectiveIntensity(float Daylight, float TimeSeconds) const
{
	if (Intensity <= 0.0f)
	{
		return 0.0f;
	}

	float Result = Intensity;

	if (FlickerAmplitude > 0.0f)
	{
		// Two incommensurable frequencies so the loop never becomes audible as a pulse.
		const float Phase = TimeSeconds * FlickerSpeed + static_cast<float>(GetUniqueID() % 1024);
		const float Flicker = FMath::Sin(Phase) * 0.65f + FMath::Sin(Phase * 1.7f) * 0.35f;
		Result *= 1.0f + Flicker * FlickerAmplitude;
	}

	if (bFadeInDaylight)
	{
		Result *= FMath::Clamp(1.0f - Daylight, 0.0f, 1.0f);
	}

	return FMath::Max(Result, 0.0f);
}
