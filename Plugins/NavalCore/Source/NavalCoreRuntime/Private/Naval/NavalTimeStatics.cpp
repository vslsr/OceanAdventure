// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalTimeStatics.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

namespace NavalTime
{
	double GetNetworkTimeSeconds(const UObject* WorldContextObject)
	{
		const UWorld* World = GEngine
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr;
		if (!World)
		{
			return 0.0;
		}

		if (const AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->GetServerWorldTimeSeconds();
		}

		return World->GetTimeSeconds();
	}
}
