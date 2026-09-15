// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Highest number of point lights that can tint the world in one frame. */
#define LINE_ART_MAX_POINT_LIGHTS 4

/**
 * Parameter names inside MPC_LineArtEnvironment.
 *
 * This list is the contract between three places that cannot include each other:
 *
 *   1. these constants, written every frame by ULineArtEnvironmentSubsystem;
 *   2. Content/Python/CreateLineArtCoreAssets.py, which authors the collection asset;
 *   3. Shaders/LineArtEnvironment.ush, which reads them through the Custom node inputs.
 *
 * A name that drifts in one place fails silently everywhere else -- the material keeps
 * compiling and simply renders with a stale value. The subsystem therefore checks every
 * write and logs the first miss per name; see LogLineArtCore.
 */
namespace LineArtParameterNames
{
	// Scalars.
	extern LINEARTCORERUNTIME_API const FName Daylight;
	extern LINEARTCORERUNTIME_API const FName GridOpacity;
	extern LINEARTCORERUNTIME_API const FName OutlineThickness;
	extern LINEARTCORERUNTIME_API const FName FogNear;
	extern LINEARTCORERUNTIME_API const FName FogFar;
	extern LINEARTCORERUNTIME_API const FName ScatterStrength;
	extern LINEARTCORERUNTIME_API const FName CloudShadowStrength;

	// Vectors.
	extern LINEARTCORERUNTIME_API const FName InkColor;
	extern LINEARTCORERUNTIME_API const FName GridColor;
	extern LINEARTCORERUNTIME_API const FName AmbientColor;
	extern LINEARTCORERUNTIME_API const FName SunDirection;
	extern LINEARTCORERUNTIME_API const FName SkyTint;
	extern LINEARTCORERUNTIME_API const FName BounceTint;
	extern LINEARTCORERUNTIME_API const FName ScatterColor;
	extern LINEARTCORERUNTIME_API const FName FogColor;
	extern LINEARTCORERUNTIME_API const FName CloudShadowOffset;

	/** PointLight<N>Position: xyz = world location, w = radius in centimetres. */
	LINEARTCORERUNTIME_API FName PointLightPosition(int32 Index);
	/** PointLight<N>Color: rgb = near colour, a = this frame's intensity (0 means empty slot). */
	LINEARTCORERUNTIME_API FName PointLightColor(int32 Index);
	/** PointLight<N>EdgeColor: rgb = colour at the falloff edge. */
	LINEARTCORERUNTIME_API FName PointLightEdgeColor(int32 Index);
}
