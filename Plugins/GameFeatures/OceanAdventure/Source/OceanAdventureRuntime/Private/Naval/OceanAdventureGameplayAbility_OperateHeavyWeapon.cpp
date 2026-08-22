// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_OperateHeavyWeapon.h"

#include "GameFramework/PlayerController.h"
#include "Naval/NavalHeavyWeaponActor.h"
#include "Naval/OceanAdventureNavalStatics.h"
#include "Naval/OceanAdventureNavalTags.h"

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

	return UOceanAdventureNavalStatics::FindNearestStationActor(
		this, Avatar->GetActorLocation(), StationSearchRadius, ANavalHeavyWeaponActor::StaticClass());
}

bool UOceanAdventureGameplayAbility_OperateHeavyWeapon::ServerOccupyStation(AActor* Station)
{
	ANavalHeavyWeaponActor* Weapon = Cast<ANavalHeavyWeaponActor>(Station);
	return Weapon && Weapon->TryOccupy(GetAvatarActorFromActorInfo());
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
