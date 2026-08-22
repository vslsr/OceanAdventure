// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureNavalSettings.h"

#include "Naval/OceanAdventureNavalTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureNavalSettings)

UOceanAdventureNavalSettings::UOceanAdventureNavalSettings()
{
	DamageSetByCallerTag = OceanAdventureNavalTags::SetByCaller_Naval_Damage;
}

const UOceanAdventureNavalSettings& UOceanAdventureNavalSettings::Get()
{
	const UOceanAdventureNavalSettings* Settings = GetDefault<UOceanAdventureNavalSettings>();
	check(Settings);
	return *Settings;
}
