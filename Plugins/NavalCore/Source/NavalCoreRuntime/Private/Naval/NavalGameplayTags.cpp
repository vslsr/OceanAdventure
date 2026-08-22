// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalGameplayTags.h"

namespace NavalGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(Message_Vessel_State, "Naval.Message.Vessel.State");
	UE_DEFINE_GAMEPLAY_TAG(Message_Vessel_Load, "Naval.Message.Vessel.Load");
	UE_DEFINE_GAMEPLAY_TAG(Message_Vessel_Part, "Naval.Message.Vessel.Part");
	UE_DEFINE_GAMEPLAY_TAG(Message_Vessel_Helm, "Naval.Message.Vessel.Helm");
	UE_DEFINE_GAMEPLAY_TAG(Message_Vessel_Alert, "Naval.Message.Vessel.Alert");
	UE_DEFINE_GAMEPLAY_TAG(Message_Shot_Blocked, "Naval.Message.Shot.Blocked");
	UE_DEFINE_GAMEPLAY_TAG(Message_HeavyWeapon_State, "Naval.Message.HeavyWeapon.State");
	UE_DEFINE_GAMEPLAY_TAG(Message_Projectile_Impact, "Naval.Message.Projectile.Impact");

	UE_DEFINE_GAMEPLAY_TAG(Alert_Boarded, "Naval.Alert.Boarded");
	UE_DEFINE_GAMEPLAY_TAG(Alert_CoreContested, "Naval.Alert.CoreContested");
	UE_DEFINE_GAMEPLAY_TAG(Alert_DangerousDraft, "Naval.Alert.DangerousDraft");
	UE_DEFINE_GAMEPLAY_TAG(Alert_Foundering, "Naval.Alert.Foundering");
	UE_DEFINE_GAMEPLAY_TAG(Alert_HeavyWeaponDeploying, "Naval.Alert.HeavyWeaponDeploying");

	UE_DEFINE_GAMEPLAY_TAG(Fail_SevereOverload, "Naval.Fail.SevereOverload");
	UE_DEFINE_GAMEPLAY_TAG(Fail_NotOperational, "Naval.Fail.NotOperational");
	UE_DEFINE_GAMEPLAY_TAG(Fail_WrongTeam, "Naval.Fail.WrongTeam");
	UE_DEFINE_GAMEPLAY_TAG(Fail_SeatOccupied, "Naval.Fail.SeatOccupied");
	UE_DEFINE_GAMEPLAY_TAG(Fail_MinimumRange, "Naval.Fail.MinimumRange");
	UE_DEFINE_GAMEPLAY_TAG(Fail_MuzzleBlocked, "Naval.Fail.MuzzleBlocked");
	UE_DEFINE_GAMEPLAY_TAG(Fail_UnderConstruction, "Naval.Fail.UnderConstruction");
	UE_DEFINE_GAMEPLAY_TAG(Fail_Reloading, "Naval.Fail.Reloading");
	UE_DEFINE_GAMEPLAY_TAG(Fail_RepairLocked, "Naval.Fail.RepairLocked");
	UE_DEFINE_GAMEPLAY_TAG(Fail_RepairSpent, "Naval.Fail.RepairSpent");
	UE_DEFINE_GAMEPLAY_TAG(Fail_TooFar, "Naval.Fail.TooFar");
	UE_DEFINE_GAMEPLAY_TAG(Fail_GroundUnsuitable, "Naval.Fail.GroundUnsuitable");
	UE_DEFINE_GAMEPLAY_TAG(Fail_LimitReached, "Naval.Fail.LimitReached");
}
