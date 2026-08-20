// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/OceanGenerationSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanGenerationSettings)

namespace
{
	constexpr float ShorelineMask = 0.02f;
	constexpr float BeachRoughnessScale = 0.2f;

	void AccumulateWave(
		const FVector2D& WorldPosition,
		float TimeSeconds,
		float Amplitude,
		float WaveLength,
		float WaveSpeed,
		const FVector2D& Direction,
		float& HeightOffset,
		FVector2D& HeightGradient,
		float& VerticalVelocity)
	{
		if (Amplitude <= UE_SMALL_NUMBER || WaveLength <= UE_SMALL_NUMBER)
		{
			return;
		}

		const FVector2D SafeDirection = Direction.GetSafeNormal();
		if (SafeDirection.IsNearlyZero())
		{
			return;
		}

		const float WaveNumber = 2.0f * UE_PI / WaveLength;
		const float AngularFrequency = WaveNumber * FMath::Max(0.0f, WaveSpeed);
		const float Phase = FVector2D::DotProduct(WorldPosition, SafeDirection) * WaveNumber
			+ TimeSeconds * AngularFrequency;
		float PhaseSin = 0.0f;
		float PhaseCos = 1.0f;
		FMath::SinCos(&PhaseSin, &PhaseCos, Phase);

		HeightOffset += Amplitude * PhaseSin;
		HeightGradient += SafeDirection * (Amplitude * WaveNumber * PhaseCos);
		VerticalVelocity += Amplitude * AngularFrequency * PhaseCos;
	}

	float HashIslandCell(int32 CellX, int32 CellY, int32 Seed, int32 Salt)
	{
		uint32 Hash = static_cast<uint32>(CellX) * 374761393u
			+ static_cast<uint32>(CellY) * 668265263u
			+ static_cast<uint32>(Seed) * 2246822519u
			+ static_cast<uint32>(Salt) * 3266489917u;
		Hash = (Hash ^ (Hash >> 13)) * 1274126177u;
		Hash ^= Hash >> 16;
		return static_cast<float>(Hash & 0xFFFFFFu) / static_cast<float>(0xFFFFFF);
	}

	float IslandProfileMask(
		float PlateauRadius, float ShorelineRadius, float OceanFloorRadius, float Distance)
	{
		if (Distance <= PlateauRadius)
		{
			return 1.0f;
		}

		if (Distance <= ShorelineRadius)
		{
			const float LandAlpha = FMath::Clamp(
				(Distance - PlateauRadius)
					/ FMath::Max(ShorelineRadius - PlateauRadius, UE_SMALL_NUMBER),
				0.0f,
				1.0f);
			return FMath::Lerp(1.0f, ShorelineMask, LandAlpha);
		}

		const float UnderwaterAlpha = FMath::Clamp(
			(Distance - ShorelineRadius)
				/ FMath::Max(OceanFloorRadius - ShorelineRadius, UE_SMALL_NUMBER),
			0.0f,
			1.0f);
		return FMath::Lerp(ShorelineMask, 0.0f, UnderwaterAlpha);
	}

	float GetIslandShapeHeight(
		float Mask, float OceanFloorHeight, float WaterHeight, float IslandPeakHeight)
	{
		if (Mask <= ShorelineMask)
		{
			const float UnderwaterAlpha = FMath::Clamp(Mask / ShorelineMask, 0.0f, 1.0f);
			const float SmoothUnderwaterAlpha = UnderwaterAlpha * UnderwaterAlpha
				* (3.0f - 2.0f * UnderwaterAlpha);
			return FMath::Lerp(OceanFloorHeight, WaterHeight, SmoothUnderwaterAlpha);
		}

		const float LandAlpha = FMath::Clamp(
			(Mask - ShorelineMask) / (1.0f - ShorelineMask), 0.0f, 1.0f);
		return FMath::Lerp(WaterHeight, IslandPeakHeight, LandAlpha);
	}
}

float UOceanGenerationSettings::SampleIslandMask(FVector2D WorldPosition) const
{
	const float CellSize = GetIslandCellSize();
	const float NominalRadius = GetIslandRadius();
	const int32 BaseCellX = FMath::FloorToInt32(WorldPosition.X / CellSize);
	const int32 BaseCellY = FMath::FloorToInt32(WorldPosition.Y / CellSize);

	float BestMask = 0.0f;
	for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
	{
		for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
		{
			const int32 CellX = BaseCellX + OffsetX;
			const int32 CellY = BaseCellY + OffsetY;
			if (HashIslandCell(CellX, CellY, WorldSeed, 3) > GetIslandDensity())
			{
				continue;
			}

			const FVector2D Center(
				(static_cast<double>(CellX) + HashIslandCell(CellX, CellY, WorldSeed, 0)) * CellSize,
				(static_cast<double>(CellY) + HashIslandCell(CellX, CellY, WorldSeed, 1)) * CellSize);
			const float ShorelineRadius = NominalRadius
				* (0.6f + 0.8f * HashIslandCell(CellX, CellY, WorldSeed, 2));
			const float OceanFloorRadius = ShorelineRadius + GetUnderwaterShelfWidth();
			const float ConfiguredPlateauRadius = ShorelineRadius * GetIslandPlateauRadiusRatio();
			const float HeightAboveWater = FMath::Max(0.0f, GetIslandPeakHeight() - GetWaterHeight());
			const float MinimumTransitionWidth = HeightAboveWater
				/ FMath::Tan(FMath::DegreesToRadians(GetBeachSlopeAngleDegrees()));
			const float PlateauRadius = FMath::Min(
				ConfiguredPlateauRadius, FMath::Max(0.0f, ShorelineRadius - MinimumTransitionWidth));
			const FVector2D ShapeOffset(
				HashIslandCell(CellX, CellY, WorldSeed, 4) * 64.0f,
				HashIslandCell(CellX, CellY, WorldSeed, 5) * 64.0f);
			const float ShapeNoise = FMath::PerlinNoise2D(
				WorldPosition / (ShorelineRadius * 1.5f) + ShapeOffset);
			const float RadialDistance = static_cast<float>(
				FVector2D::Distance(WorldPosition, Center));
			const float WarpAlpha = FMath::Clamp(
				(RadialDistance - PlateauRadius)
					/ FMath::Max(ShorelineRadius - PlateauRadius, UE_SMALL_NUMBER),
				0.0f,
				1.0f);
			const float WarpWeight = WarpAlpha * WarpAlpha * (3.0f - 2.0f * WarpAlpha);
			const float Distance = RadialDistance
				+ ShapeNoise * ShorelineRadius * GetIslandEdgeDistortion() * WarpWeight;

			BestMask = FMath::Max(BestMask, IslandProfileMask(
				PlateauRadius, ShorelineRadius, OceanFloorRadius, Distance));
		}
	}

	return FMath::Clamp(BestMask, 0.0f, 1.0f);
}

float UOceanGenerationSettings::SampleTerrainHeight(FVector2D WorldPosition) const
{
	const float Seed = static_cast<float>(WorldSeed);
	const FVector2D SeedOffset(Seed * 0.0137f, Seed * 0.0211f);
	const float NoiseScale = GetTerrainNoiseScale();
	const float MacroNoise = FMath::PerlinNoise2D(WorldPosition / NoiseScale + SeedOffset);
	const float DetailNoise = FMath::PerlinNoise2D(
		WorldPosition / (NoiseScale * 0.28f) + SeedOffset * 1.7f);
	const float Roughness = MacroNoise * 0.78f + DetailNoise * 0.22f;
	const float Mask = SampleIslandMask(WorldPosition);
	const float Shape = GetIslandShapeHeight(
		Mask, GetOceanFloorHeight(), GetWaterHeight(), GetIslandPeakHeight());
	const float LandAlpha = FMath::Clamp(
		(Mask - ShorelineMask) / (1.0f - ShorelineMask), 0.0f, 1.0f);
	const float TransitionWeight = 4.0f * LandAlpha * (1.0f - LandAlpha);
	return Shape + Roughness * GetTerrainHeightAmplitude() * BeachRoughnessScale * TransitionWeight;
}

FOceanWaterSurfaceSample UOceanGenerationSettings::SampleWaterSurface(
	FVector2D WorldPosition, float TimeSeconds) const
{
	float HeightOffset = 0.0f;
	float VerticalVelocity = 0.0f;
	FVector2D HeightGradient = FVector2D::ZeroVector;

	AccumulateWave(
		WorldPosition,
		TimeSeconds,
		PrimaryWaveAmplitude,
		PrimaryWaveLength,
		PrimaryWaveSpeed,
		PrimaryWaveDirection,
		HeightOffset,
		HeightGradient,
		VerticalVelocity);
	AccumulateWave(
		WorldPosition,
		TimeSeconds,
		SecondaryWaveAmplitude,
		SecondaryWaveLength,
		SecondaryWaveSpeed,
		SecondaryWaveDirection,
		HeightOffset,
		HeightGradient,
		VerticalVelocity);

	FOceanWaterSurfaceSample Result;
	Result.Position = FVector(WorldPosition.X, WorldPosition.Y, GetWaterHeight() + HeightOffset);
	Result.Normal = FVector(-HeightGradient.X, -HeightGradient.Y, 1.0f).GetSafeNormal();
	Result.Velocity = FVector(0.0f, 0.0f, VerticalVelocity);
	Result.bIsValid = true;
	return Result;
}
