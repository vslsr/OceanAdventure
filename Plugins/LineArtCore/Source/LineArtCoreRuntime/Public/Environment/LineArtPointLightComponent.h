// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"

#include "LineArtPointLightComponent.generated.h"

/**
 * A campfire-style light for the line-art fill shader.
 *
 * Deliberately NOT a UPointLightComponent: the fill materials are Unlit, so an engine
 * light would cost a full mobile lighting pass and still contribute nothing. This one is
 * pure data -- the environment subsystem picks the few nearest to the view each frame and
 * writes them into the shared parameter collection.
 *
 * Place as many as the world wants. The shader loop length is fixed at
 * LINE_ART_MAX_POINT_LIGHTS and does not grow with the number of campfires.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (LineArt), meta = (BlueprintSpawnableComponent))
class LINEARTCORERUNTIME_API ULineArtPointLightComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	ULineArtPointLightComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	/** Reach in centimetres. Beyond this the light contributes nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art|Point Light", meta = (ClampMin = "0.0"))
	float Radius = 600.0f;

	/** Colour close to the flame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art|Point Light")
	FLinearColor Color = FLinearColor(1.0f, 0.68f, 0.34f);

	/** Colour at the falloff edge; the gap between the two is what reads as embers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art|Point Light")
	FLinearColor EdgeColor = FLinearColor(0.85f, 0.32f, 0.16f);

	/** Base intensity. 0 means unlit -- the slot is then treated as empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art|Point Light", meta = (ClampMin = "0.0"))
	float Intensity = 1.0f;

	/** Peak flicker amount as a fraction of Intensity. 0 keeps the light perfectly steady. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art|Point Light", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlickerAmplitude = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art|Point Light", meta = (ClampMin = "0.0"))
	float FlickerSpeed = 6.5f;

	/** Fade the light out as the sun comes up, so noon is not dotted with orange blobs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art|Point Light")
	bool bFadeInDaylight = true;

	/**
	 * Intensity for one frame, with flicker and daylight falloff already folded in.
	 *
	 * Folded per light rather than per pixel: the reference implementation moved both out
	 * of the shader for exactly this reason -- they are the same value for every pixel the
	 * light touches, so evaluating them per fragment buys nothing.
	 */
	UFUNCTION(BlueprintPure, Category = "Line Art|Point Light")
	float GetEffectiveIntensity(float Daylight, float TimeSeconds) const;
};
