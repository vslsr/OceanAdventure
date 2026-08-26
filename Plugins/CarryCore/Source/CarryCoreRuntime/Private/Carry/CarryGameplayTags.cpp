// Copyright Epic Games, Inc. All Rights Reserved.

#include "Carry/CarryGameplayTags.h"

namespace CarryGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(Carry_Class_Haulable, "Carry.Class.Haulable");

	UE_DEFINE_GAMEPLAY_TAG(Fail_Carry_NoTarget, "Carry.Fail.NoTarget");
	UE_DEFINE_GAMEPLAY_TAG(Fail_Carry_TooFar, "Carry.Fail.TooFar");
	UE_DEFINE_GAMEPLAY_TAG(Fail_Carry_Occupied, "Carry.Fail.Occupied");
	UE_DEFINE_GAMEPLAY_TAG(Fail_Carry_HandsFull, "Carry.Fail.HandsFull");
	UE_DEFINE_GAMEPLAY_TAG(Fail_Carry_NotCarrying, "Carry.Fail.NotCarrying");
	UE_DEFINE_GAMEPLAY_TAG(Fail_Carry_Invalid, "Carry.Fail.Invalid");
}
