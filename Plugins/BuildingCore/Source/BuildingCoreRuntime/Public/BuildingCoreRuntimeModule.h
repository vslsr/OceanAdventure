// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

BUILDINGCORERUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogBuildingCore, Log, All);

class FBuildingCoreRuntimeModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
