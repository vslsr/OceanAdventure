// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;

/**
 * Team resolution for the framework layer.
 *
 * Reads IGenericTeamAgentInterface, which Lyra's own team agents implement, so ballistics
 * and ownership rules work without NavalCore knowing that LyraTeamSubsystem exists.
 */
namespace NavalTeam
{
	/** Walks actor -> instigator controller -> owner until something reports a team. */
	NAVALCORERUNTIME_API int32 GetTeamId(const AActor* Actor);

	/** True only when both sides resolved to the same valid team. */
	NAVALCORERUNTIME_API bool AreFriendly(const AActor* A, const AActor* B);

	inline bool IsValidTeam(int32 TeamId) { return TeamId != INDEX_NONE; }
}
