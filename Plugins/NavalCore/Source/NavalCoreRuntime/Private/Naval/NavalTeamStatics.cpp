// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalTeamStatics.h"

#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GenericTeamAgentInterface.h"

namespace NavalTeam
{
	static int32 ReadTeamId(const UObject* Object)
	{
		const IGenericTeamAgentInterface* Agent = Cast<IGenericTeamAgentInterface>(Object);
		if (!Agent)
		{
			return INDEX_NONE;
		}

		const FGenericTeamId TeamId = Agent->GetGenericTeamId();
		return TeamId == FGenericTeamId::NoTeam ? INDEX_NONE : static_cast<int32>(TeamId.GetId());
	}

	int32 GetTeamId(const AActor* Actor)
	{
		// A projectile, a placed wall or a cannon carries no team of its own; the chain
		// below is what turns it into the team that owns it.
		for (const AActor* Current = Actor; Current != nullptr; Current = Current->GetOwner())
		{
			const int32 DirectTeam = ReadTeamId(Current);
			if (IsValidTeam(DirectTeam))
			{
				return DirectTeam;
			}

			if (const APawn* Pawn = Cast<APawn>(Current))
			{
				const int32 ControllerTeam = ReadTeamId(Pawn->GetController());
				if (IsValidTeam(ControllerTeam))
				{
					return ControllerTeam;
				}

				const int32 StateTeam = ReadTeamId(Pawn->GetPlayerState());
				if (IsValidTeam(StateTeam))
				{
					return StateTeam;
				}
			}

			const int32 InstigatorTeam = ReadTeamId(Current->GetInstigatorController());
			if (IsValidTeam(InstigatorTeam))
			{
				return InstigatorTeam;
			}
		}

		return INDEX_NONE;
	}

	bool AreFriendly(const AActor* A, const AActor* B)
	{
		const int32 TeamA = GetTeamId(A);
		const int32 TeamB = GetTeamId(B);
		return IsValidTeam(TeamA) && TeamA == TeamB;
	}
}
