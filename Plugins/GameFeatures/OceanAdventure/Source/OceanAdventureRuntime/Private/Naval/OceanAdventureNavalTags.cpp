// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureNavalTags.h"

namespace OceanAdventureNavalTags
{
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Naval_Interact, "InputTag.Naval.Interact");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Naval_Fire, "InputTag.Naval.Fire");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Naval_Leave, "InputTag.Naval.Leave");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Naval_Repair, "InputTag.Naval.Repair");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Naval_DeployHeavyWeapon, "InputTag.Naval.DeployHeavyWeapon");
	UE_DEFINE_GAMEPLAY_TAG(InputTag_Naval_DeployLifeRaft, "InputTag.Naval.DeployLifeRaft");

	UE_DEFINE_GAMEPLAY_TAG(Status_Naval_Steering, "Status.Naval.Steering");
	UE_DEFINE_GAMEPLAY_TAG(Status_Naval_OperatingHeavyWeapon, "Status.Naval.OperatingHeavyWeapon");
	UE_DEFINE_GAMEPLAY_TAG(Status_Naval_StationExitLock, "Status.Naval.StationExitLock");
	UE_DEFINE_GAMEPLAY_TAG(Status_Naval_StationAvailable, "Status.Naval.StationAvailable");
	UE_DEFINE_GAMEPLAY_TAG(Status_Naval_Repairing, "Status.Naval.Repairing");

	UE_DEFINE_GAMEPLAY_TAG(Phase_NavalP0_Warmup, "GamePhase.NavalP0.Warmup");
	UE_DEFINE_GAMEPLAY_TAG(Phase_NavalP0_Skirmish, "GamePhase.NavalP0.Skirmish");
	UE_DEFINE_GAMEPLAY_TAG(Phase_NavalP0_SuddenDeath, "GamePhase.NavalP0.SuddenDeath");
	UE_DEFINE_GAMEPLAY_TAG(Phase_NavalP0_PostGame, "GamePhase.NavalP0.PostGame");

	UE_DEFINE_GAMEPLAY_TAG(Message_Naval_MatchState, "Naval.Message.Match.State");
	UE_DEFINE_GAMEPLAY_TAG(Message_Naval_Beacon, "Naval.Message.Match.Beacon");
	UE_DEFINE_GAMEPLAY_TAG(Message_Naval_StationFailed, "Naval.Message.Station.Failed");
	UE_DEFINE_GAMEPLAY_TAG(Message_Naval_StationPrompt, "Naval.Message.Station.Prompt");

	UE_DEFINE_GAMEPLAY_TAG(SetByCaller_Naval_Damage, "SetByCaller.Naval.Damage");
}
