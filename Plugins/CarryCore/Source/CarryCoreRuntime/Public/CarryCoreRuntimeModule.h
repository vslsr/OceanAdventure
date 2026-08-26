// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

CARRYCORERUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogCarryCore, Log, All);

class FCarryCoreRuntimeModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
