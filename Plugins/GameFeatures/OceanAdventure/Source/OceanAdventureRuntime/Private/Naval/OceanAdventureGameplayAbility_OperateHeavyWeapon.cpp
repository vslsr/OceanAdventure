// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_OperateHeavyWeapon.h"

#include "GameFramework/PlayerController.h"
#include "Naval/NavalHeavyWeaponActor.h"
#include "Naval/OceanAdventureNavalStatics.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_OperateHeavyWeapon)

UOceanAdventureGameplayAbility_OperateHeavyWeapon::UOceanAdventureGameplayAbility_OperateHeavyWeapon(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Traverse is slow enough that a quarter-second of aim lag would be visible; this is one
	// of the few places worth sampling at close to frame rate.
	ControlSampleInterval = 0.05f;
}

AActor* UOceanAdventureGameplayAbility_OperateHeavyWeapon::FindStationInRange() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return nullptr;
	}

	AActor* Station = UOceanAdventureNavalStatics::FindNearestStationActor(
		this, Avatar->GetActorLocation(), StationSearchRadius, ANavalHeavyWeaponActor::StaticClass());
	UE_LOG(LogOceanAdventure, Display,
		TEXT("[HeavyWeaponOperate] Search avatar=%s origin=%s radius=%.1f result=%s distance=%.1f"),
		*GetNameSafe(Avatar), *Avatar->GetActorLocation().ToCompactString(), StationSearchRadius,
		*GetNameSafe(Station), Station ? FVector::Dist(Avatar->GetActorLocation(), Station->GetActorLocation()) : -1.0f);
	return Station;
}

bool UOceanAdventureGameplayAbility_OperateHeavyWeapon::ServerOccupyStation(AActor* Station)
{
	ANavalHeavyWeaponActor* Weapon = Cast<ANavalHeavyWeaponActor>(Station);
	AActor* Avatar = GetAvatarActorFromActorInfo();
	FGameplayTag FailReason;
	const bool bCanOperate = Weapon && Weapon->CanOperate(Avatar, FailReason);
	UE_LOG(LogOceanAdventure, Display,
		TEXT("[HeavyWeaponOperate] Server validate weapon=%s avatar=%s can_operate=%d reason=%s distance=%.1f operator=%s"),
		*GetNameSafe(Weapon), *GetNameSafe(Avatar), bCanOperate, *FailReason.ToString(),
		Weapon && Avatar ? FVector::Dist(Weapon->GetActorLocation(), Avatar->GetActorLocation()) : -1.0f,
		*GetNameSafe(Weapon ? Weapon->GetWeaponOperator() : nullptr));
	return bCanOperate && Weapon->TryOccupy(Avatar);
}

void UOceanAdventureGameplayAbility_OperateHeavyWeapon::ServerReleaseStation(AActor* Station)
{
	if (ANavalHeavyWeaponActor* Weapon = Cast<ANavalHeavyWeaponActor>(Station))
	{
		Weapon->ReleaseOperator(GetAvatarActorFromActorInfo());
	}
}

void UOceanAdventureGameplayAbility_OperateHeavyWeapon::ServerApplyControl(
	AActor* Station, const FOceanAdventureNavalTargetData& Data)
{
	if (ANavalHeavyWeaponActor* Weapon = Cast<ANavalHeavyWeaponActor>(Station))
	{
		UE_LOG(LogOceanAdventure, VeryVerbose,
			TEXT("[HeavyWeaponOperate] Aim sample weapon=%s avatar=%s aim=%s muzzle_dir=%s"),
			*GetNameSafe(Weapon), *GetNameSafe(GetAvatarActorFromActorInfo()),
			*Data.AimLocation.ToString(), *Weapon->GetMuzzleDirection().ToString());
		// Clamped to the traverse arc inside the weapon; the client cannot aim past it by
		// sending a point behind the mount.
		Weapon->SetDesiredAimLocation(GetAvatarActorFromActorInfo(), Data.AimLocation);
	}
}

bool UOceanAdventureGameplayAbility_OperateHeavyWeapon::BuildControlSample(
	FOceanAdventureNavalTargetData& OutData) const
{
	APlayerController* PlayerController = Cast<APlayerController>(GetControllerFromActorInfo());
	FVector AimLocation = FVector::ZeroVector;
	if (!UOceanAdventureNavalStatics::GetCursorAimLocation(PlayerController, AimLocation))
	{
		return false;
	}

	OutData.AimLocation = AimLocation;
	if (ANavalHeavyWeaponActor* Weapon = Cast<ANavalHeavyWeaponActor>(GetStationActor()))
	{
		Weapon->SetLocalPredictedAimLocation(GetAvatarActorFromActorInfo(), AimLocation);
	}
	return true;
}

FGameplayTag UOceanAdventureGameplayAbility_OperateHeavyWeapon::GetStationStatusTag() const
{
	return OceanAdventureNavalTags::Status_Naval_OperatingHeavyWeapon;
}

bool UOceanAdventureGameplayAbility_OperateHeavyWeapon::IsStationStillValid(AActor* Station) const
{
	const ANavalHeavyWeaponActor* Weapon = Cast<ANavalHeavyWeaponActor>(Station);
	if (!Weapon)
	{
		return false;
	}

	// A gun shot apart under its operator drops them back to their light weapon rather than
	// leaving them standing at a wreck.
	const AActor* CurrentOperator = Weapon->GetWeaponOperator();
	return (CurrentOperator == nullptr || CurrentOperator == GetAvatarActorFromActorInfo());
}

bool UOceanAdventureGameplayAbility_OperateHeavyWeapon::GetOperatorTransform(
	AActor* Station, FTransform& OutTransform) const
{
	const ANavalHeavyWeaponActor* Weapon = Cast<ANavalHeavyWeaponActor>(Station);
	if (!Weapon)
	{
		return false;
	}

	OutTransform = Weapon->GetOperatorTransform();
	return true;
}
