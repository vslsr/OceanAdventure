// Copyright Epic Games, Inc. All Rights Reserved.

#include "Script/ScriptHostSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ScriptHostSettings)

UScriptHostSettings::UScriptHostSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("Script Host (TypeScript)");
}

const UScriptHostSettings& UScriptHostSettings::Get()
{
	const UScriptHostSettings* Settings = GetDefault<UScriptHostSettings>();
	check(Settings);
	return *Settings;
}
