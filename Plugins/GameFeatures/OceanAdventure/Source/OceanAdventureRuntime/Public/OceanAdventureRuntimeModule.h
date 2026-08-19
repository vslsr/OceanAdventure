// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

OCEANADVENTURERUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogOceanAdventure, Log, All);

class FOceanAdventureRuntimeModule : public IModuleInterface
{
public:
	//~IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~End of IModuleInterface
};
