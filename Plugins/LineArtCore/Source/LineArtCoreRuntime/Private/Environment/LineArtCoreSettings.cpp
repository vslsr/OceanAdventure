// Copyright Epic Games, Inc. All Rights Reserved.

#include "Environment/LineArtCoreSettings.h"

ULineArtCoreSettings::ULineArtCoreSettings()
{
	CategoryName = TEXT("Game");
}

const ULineArtCoreSettings& ULineArtCoreSettings::Get()
{
	const ULineArtCoreSettings* Settings = GetDefault<ULineArtCoreSettings>();
	check(Settings);
	return *Settings;
}
