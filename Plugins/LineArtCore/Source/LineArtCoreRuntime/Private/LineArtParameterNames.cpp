// Copyright Epic Games, Inc. All Rights Reserved.

#include "LineArtParameterNames.h"

namespace LineArtParameterNames
{
	const FName Daylight(TEXT("Daylight"));
	const FName GridOpacity(TEXT("GridOpacity"));
	const FName OutlineThickness(TEXT("OutlineThickness"));
	const FName FogNear(TEXT("FogNear"));
	const FName FogFar(TEXT("FogFar"));
	const FName ScatterStrength(TEXT("ScatterStrength"));
	const FName CloudShadowStrength(TEXT("CloudShadowStrength"));

	const FName InkColor(TEXT("InkColor"));
	const FName GridColor(TEXT("GridColor"));
	const FName AmbientColor(TEXT("AmbientColor"));
	const FName SunDirection(TEXT("SunDirection"));
	const FName SkyTint(TEXT("SkyTint"));
	const FName BounceTint(TEXT("BounceTint"));
	const FName ScatterColor(TEXT("ScatterColor"));
	const FName FogColor(TEXT("FogColor"));
	const FName CloudShadowOffset(TEXT("CloudShadowOffset"));

	FName PointLightPosition(int32 Index)
	{
		return FName(*FString::Printf(TEXT("PointLight%dPosition"), Index));
	}

	FName PointLightColor(int32 Index)
	{
		return FName(*FString::Printf(TEXT("PointLight%dColor"), Index));
	}

	FName PointLightEdgeColor(int32 Index)
	{
		return FName(*FString::Printf(TEXT("PointLight%dEdgeColor"), Index));
	}
}
