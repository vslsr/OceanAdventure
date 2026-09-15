// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Environment/LineArtEnvironmentState.h"
#include "UObject/SoftObjectPtr.h"

#include "LineArtCoreSettings.generated.h"

class UMaterialParameterCollection;

/**
 * Project-wide line-art constants.
 *
 * The ink numbers are ported verbatim from SkyLand's src/materials/lineMaterials.ts and
 * are not free parameters: the night lift exists because outline lines do not participate
 * in lighting, so once the paper darkens past the ink the silhouettes vanish and the
 * screen is left with nothing but the ground grid.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Line Art Core"))
class LINEARTCORERUNTIME_API ULineArtCoreSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	ULineArtCoreSettings();

	static const ULineArtCoreSettings& Get();

	/** The collection every line-art material samples. Empty disables the subsystem. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftObjectPtr<UMaterialParameterCollection> EnvironmentCollection;

	/** Daytime ink. */
	UPROPERTY(config, EditAnywhere, Category = "Ink")
	FLinearColor BaseInkColor = FLinearColor::FromSRGBColor(FColor(0x17, 0x16, 0x14));

	/** Night ink: lighter than the paper, so the line art flips to pale-on-dark. */
	UPROPERTY(config, EditAnywhere, Category = "Ink")
	FLinearColor NightInkColor = FLinearColor::FromSRGBColor(FColor(0xc4, 0xce, 0xdd));

	UPROPERTY(config, EditAnywhere, Category = "Ink")
	FLinearColor BaseGridColor = FLinearColor::FromSRGBColor(FColor(0x9d, 0x9a, 0x90));

	/** Daylight at which the ink starts lifting off the paper. Above this, ink stays at base. */
	UPROPERTY(config, EditAnywhere, Category = "Ink", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InkLiftFrom = 0.55f;

	/** Daylight at which the lift is complete. Deliberately below InkLiftFrom. */
	UPROPERTY(config, EditAnywhere, Category = "Ink", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InkLiftTo = 0.12f;

	/** How far the grid sinks with the paper at full lift. */
	UPROPERTY(config, EditAnywhere, Category = "Ink", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NightGridScale = 0.34f;

	UPROPERTY(config, EditAnywhere, Category = "Ink", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GridBaseOpacity = 0.34f;

	/** Extra opacity the grid gives up at full lift, so objects read louder than the ruler. */
	UPROPERTY(config, EditAnywhere, Category = "Ink", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GridNightOpacityScale = 0.35f;

	/** Written to the collection before any weather system takes over. */
	UPROPERTY(config, EditAnywhere, Category = "Environment")
	FLineArtEnvironmentState DefaultEnvironment;
};
