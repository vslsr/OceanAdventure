// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "World/OceanGenerationSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOceanGenerationSettingsDeterminismTest,
	"OceanCore.Generation.SettingsDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOceanGenerationSettingsDeterminismTest::RunTest(const FString& Parameters)
{
	const UOceanGenerationSettings* Settings = NewObject<UOceanGenerationSettings>();
	TestNotNull(TEXT("Default settings can be created"), Settings);
	if (!Settings)
	{
		return false;
	}

	bool bFoundWater = false;
	bool bFoundBeach = false;
	bool bFoundLand = false;
	bool bFoundPlateau = false;
	bool bFoundTransition = false;
	bool bPlateauStaysFlat = true;
	bool bOceanFloorStaysFlat = true;
	float MaximumLandSlope = 0.0f;
	constexpr int32 SampleRadius = 200;
	constexpr float SampleSpacing = 500.0f;
	for (int32 SampleX = -SampleRadius; SampleX <= SampleRadius; ++SampleX)
	{
		for (int32 SampleY = -SampleRadius; SampleY <= SampleRadius; ++SampleY)
		{
			const FVector2D WorldPosition(SampleX * SampleSpacing, SampleY * SampleSpacing);
			const float FirstMask = Settings->SampleIslandMask(WorldPosition);
			const float SecondMask = Settings->SampleIslandMask(WorldPosition);
			const float FirstHeight = Settings->SampleTerrainHeight(WorldPosition);
			const float SecondHeight = Settings->SampleTerrainHeight(WorldPosition);
			const float HeightX = Settings->SampleTerrainHeight(
				WorldPosition + FVector2D(SampleSpacing, 0.0));
			const float HeightY = Settings->SampleTerrainHeight(
				WorldPosition + FVector2D(0.0, SampleSpacing));

			if (!FMath::IsNearlyEqual(FirstMask, SecondMask)
				|| !FMath::IsNearlyEqual(FirstHeight, SecondHeight))
			{
				AddError(FString::Printf(
					TEXT("Repeated samples diverged at (%g, %g)."), WorldPosition.X, WorldPosition.Y));
				return false;
			}

			if (FirstMask < 0.0f || FirstMask > 1.0f)
			{
				AddError(FString::Printf(
					TEXT("Island mask %g is outside [0, 1] at (%g, %g)."),
					FirstMask, WorldPosition.X, WorldPosition.Y));
				return false;
			}

			if (!FMath::IsFinite(FirstHeight))
			{
				AddError(FString::Printf(
					TEXT("Terrain height is not finite at (%g, %g)."),
					WorldPosition.X, WorldPosition.Y));
				return false;
			}

			bFoundWater |= FirstHeight <= Settings->GetWaterHeight();
			bFoundBeach |= FirstHeight > Settings->GetWaterHeight()
				&& FirstHeight <= Settings->GetWaterHeight() + Settings->GetBeachBandHeight();
			bFoundLand |= FirstHeight > Settings->GetWaterHeight() + Settings->GetBeachBandHeight();
			if (FirstHeight >= Settings->GetWaterHeight() && HeightX >= Settings->GetWaterHeight())
			{
				MaximumLandSlope = FMath::Max(
					MaximumLandSlope, FMath::Abs(HeightX - FirstHeight) / SampleSpacing);
			}
			if (FirstHeight >= Settings->GetWaterHeight() && HeightY >= Settings->GetWaterHeight())
			{
				MaximumLandSlope = FMath::Max(
					MaximumLandSlope, FMath::Abs(HeightY - FirstHeight) / SampleSpacing);
			}

			if (FirstMask == 1.0f)
			{
				bFoundPlateau = true;
				bPlateauStaysFlat &= FMath::IsNearlyEqual(
					FirstHeight, Settings->GetIslandPeakHeight(), 0.01f);
			}
			else if (FirstMask == 0.0f)
			{
				bOceanFloorStaysFlat &= FMath::IsNearlyEqual(
					FirstHeight, Settings->GetOceanFloorHeight(), 0.01f);
			}
			else
			{
				bFoundTransition = true;
			}
		}
	}

	TestTrue(TEXT("The sampled area contains water"), bFoundWater);
	TestTrue(TEXT("The sampled area contains the beach band"), bFoundBeach);
	TestTrue(TEXT("The sampled area contains land"), bFoundLand);
	TestTrue(TEXT("Island centers form an exactly flat plateau"), bFoundPlateau && bPlateauStaysFlat);
	TestTrue(TEXT("Island sides contain a smooth transition"), bFoundTransition);
	TestTrue(TEXT("The ocean floor remains flat outside islands"), bOceanFloorStaysFlat);
	TestTrue(TEXT("Above-water beach slopes remain walkable"),
		MaximumLandSlope <= FMath::Tan(FMath::DegreesToRadians(35.0f)));

	const FVector2D WaterSamplePosition(1234.0f, -5678.0f);
	const FOceanWaterSurfaceSample FirstWaterSample = Settings->SampleWaterSurface(
		WaterSamplePosition, 12.5f);
	const FOceanWaterSurfaceSample SecondWaterSample = Settings->SampleWaterSurface(
		WaterSamplePosition, 12.5f);
	TestTrue(TEXT("Water surface samples are valid"), FirstWaterSample.bIsValid);
	TestTrue(TEXT("Water surface sampling is deterministic"),
		FirstWaterSample.Position.Equals(SecondWaterSample.Position, UE_KINDA_SMALL_NUMBER)
		&& FirstWaterSample.Normal.Equals(SecondWaterSample.Normal, UE_KINDA_SMALL_NUMBER)
		&& FirstWaterSample.Velocity.Equals(SecondWaterSample.Velocity, UE_KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Water surface normals are normalized and face upward"),
		FMath::IsNearlyEqual(FirstWaterSample.Normal.SizeSquared(), 1.0f, UE_KINDA_SMALL_NUMBER)
		&& FirstWaterSample.Normal.Z > 0.0f);
	TestFalse(TEXT("Water surface samples contain no NaNs"),
		FirstWaterSample.Position.ContainsNaN()
		|| FirstWaterSample.Normal.ContainsNaN()
		|| FirstWaterSample.Velocity.ContainsNaN());

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
