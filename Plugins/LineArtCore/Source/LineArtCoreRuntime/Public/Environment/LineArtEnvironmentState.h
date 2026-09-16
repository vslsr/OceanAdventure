// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LineArtEnvironmentState.generated.h"

/**
 * One frame of scene-wide environment truth, shared by every line-art material.
 *
 * Ported from SkyLand's SceneEnvironmentRuntime (src/materials/createFillMaterial.ts).
 * Two conversions happen at this boundary and nowhere else, so everything downstream can
 * stay in engine units:
 *
 *   - lengths are centimetres here, metres in the reference implementation;
 *   - "up" is +Z here, +Y in the reference implementation (three.js).
 *
 * Both are silent-failure conversions: a fog radius that is 100x too short still renders,
 * and a hemisphere tint that reads the wrong axis just looks subtly wrong from above.
 */
USTRUCT(BlueprintType)
struct LINEARTCORERUNTIME_API FLineArtEnvironmentState
{
	GENERATED_BODY()

	/** Direction the sunlight travels FROM, normalised. Default is the fixed afternoon key light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art")
	FVector SunDirection = FVector(-0.55, 0.35, 0.9).GetSafeNormal();

	/** Relative brightness of the environment light, 1 at noon. Drives the night ink lift. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Daylight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art")
	FLinearColor AmbientColor = FLinearColor::White;

	/** Zenith tint, normalised to an average of 1: changes hue, never brightness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art")
	FLinearColor SkyTint = FLinearColor::White;

	/** Ground-bounce tint, normalised the same way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art")
	FLinearColor BounceTint = FLinearColor::White;

	/** Ink tint, normalised the same way: cool at night, warm at dusk, never thinner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art")
	FLinearColor InkTint = FLinearColor::White;

	/** Colour the fog takes when looking towards the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art")
	FLinearColor ScatterColor = FLinearColor::White;

	/** 0 keeps the fog a flat sky colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ScatterStrength = 0.0f;

	/** Deepest cloud-shadow darkening. 0 disables the noise entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CloudShadowStrength = 0.0f;

	/** Scroll offset of the cloud-shadow noise; advanced by the wind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art")
	FVector2D CloudShadowOffset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art")
	FLinearColor FogColor = FLinearColor(0.992f, 0.984f, 0.965f);

	/** Centimetres. SkyLand's defaults were 22 m / 52 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art", meta = (ClampMin = "0.0"))
	float FogNear = 2200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art", meta = (ClampMin = "0.0"))
	float FogFar = 5200.0f;

	/**
	 * Outline width in pixels at the screen's vertical centre.
	 *
	 * Screen-space constant on purpose: WebGL could not vary line width at all, so the
	 * whole palette was tuned against a fixed 1px line. Widening it here re-opens a
	 * balance question the reference art never had to answer -- see the migration doc.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line Art", meta = (ClampMin = "0.0"))
	float OutlineThickness = 1.0f;
};
