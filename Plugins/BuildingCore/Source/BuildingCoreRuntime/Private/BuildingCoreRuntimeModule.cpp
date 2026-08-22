// Copyright Epic Games, Inc. All Rights Reserved.

#include "BuildingCoreRuntimeModule.h"

#include "Building/BuildCheats.h"
#include "GameFramework/CheatManager.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY(LogBuildingCore);

void FBuildingCoreRuntimeModule::StartupModule()
{
#if UE_WITH_CHEAT_MANAGER
	// Force construction of the CDO while the module starts so it can register the
	// extension for cheat managers created after this module loads.
	UBuildCheats::StaticClass()->GetDefaultObject();

	// BuildingCore may be loaded by a GameFeature after PIE has already created the local
	// player's CheatManager. The creation delegate cannot observe past instances, so repair
	// every live manager here as well. FindCheatManagerExtension keeps this idempotent.
	for (TObjectIterator<UCheatManager> It; It; ++It)
	{
		UCheatManager* CheatManager = *It;
		if (!IsValid(CheatManager)
			|| CheatManager->HasAnyFlags(RF_ClassDefaultObject)
			|| CheatManager->FindCheatManagerExtension<UBuildCheats>())
		{
			continue;
		}

		CheatManager->AddCheatManagerExtension(NewObject<UBuildCheats>(CheatManager));
		UE_LOG(
			LogBuildingCore,
			Display,
			TEXT("Attached BuildCheats to existing CheatManager %s"),
			*GetNameSafe(CheatManager));
	}
#endif
}

void FBuildingCoreRuntimeModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FBuildingCoreRuntimeModule, BuildingCoreRuntime)
