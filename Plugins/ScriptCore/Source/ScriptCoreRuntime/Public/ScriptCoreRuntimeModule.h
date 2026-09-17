// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

SCRIPTCORERUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogScriptCore, Log, All);

class FScriptCoreRuntimeModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
