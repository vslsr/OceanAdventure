// Copyright Epic Games, Inc. All Rights Reserved.

#include "BuildingCoreRuntimeModule.h"

#include "Building/BuildCheats.h"

DEFINE_LOG_CATEGORY(LogBuildingCore);

void FBuildingCoreRuntimeModule::StartupModule()
{
#if UE_WITH_CHEAT_MANAGER
	// Force construction of the CDO while the module starts so it can register the
	// extension before the first player controller creates its cheat manager.
	UBuildCheats::StaticClass()->GetDefaultObject();
#endif
}

void FBuildingCoreRuntimeModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FBuildingCoreRuntimeModule, BuildingCoreRuntime)
