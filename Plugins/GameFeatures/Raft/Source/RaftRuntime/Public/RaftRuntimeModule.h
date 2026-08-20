// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

RAFTRUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogRaft, Log, All);

class FRaftRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
