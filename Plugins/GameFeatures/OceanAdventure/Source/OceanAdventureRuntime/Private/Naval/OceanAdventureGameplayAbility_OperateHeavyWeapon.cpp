// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_OperateHeavyWeapon.h"

#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
#include "Naval/NavalHeavyWeaponActor.h"
#include "Naval/OceanAdventureGameplayAbility_FireHeavyWeapon.h"
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

void UOceanAdventureGameplayAbility_OperateHeavyWeapon::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// ActiveStation is weak and may already be gone when a destroyed cannon ends the
	// operation. Revoke independently of the station pointer so the mouse-fire spec cannot
	// leak onto the player.
	if (HasAuthority(&ActivationInfo))
	{
		RevokeFireAbility();
	}

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
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
	if (!bCanOperate || !Weapon->TryOccupy(Avatar))
	{
		return false;
	}

	// The interact ability is permanent, but firing only exists for the occupied gun. The
	// replicated spec carries both InputTag.Naval.Fire and the weapon Actor as SourceObject,
	// so the owning client receives a normal Lyra mouse binding without another RPC.
	if (!GrantFireAbility(Weapon))
	{
		Weapon->ReleaseOperator(Avatar);
		return false;
	}

	return true;
}

void UOceanAdventureGameplayAbility_OperateHeavyWeapon::ServerReleaseStation(AActor* Station)
{
	RevokeFireAbility();

	if (ANavalHeavyWeaponActor* Weapon = Cast<ANavalHeavyWeaponActor>(Station))
	{
		Weapon->ReleaseOperator(GetAvatarActorFromActorInfo());
	}
}

bool UOceanAdventureGameplayAbility_OperateHeavyWeapon::GrantFireAbility(
	ANavalHeavyWeaponActor* Weapon)
{
	// This ability is instantiated from the player's ASC.  The cannon is deliberately not
	// given an ASC: it is only the world-owned target carried by the player's fire spec.
	ULyraAbilitySystemComponent* AbilitySystem = GetLyraAbilitySystemComponentFromActorInfo();
	if (!HasAuthority(&CurrentActivationInfo) || !AbilitySystem || !Weapon)
	{
		return false;
	}

	RevokeFireAbility();

	UOceanAdventureGameplayAbility_FireHeavyWeapon* FireAbilityCDO =
		UOceanAdventureGameplayAbility_FireHeavyWeapon::StaticClass()
			->GetDefaultObject<UOceanAdventureGameplayAbility_FireHeavyWeapon>();
	FGameplayAbilitySpec FireSpec(FireAbilityCDO, /*Level=*/1);
	FireSpec.SourceObject = Weapon;
	FireSpec.GetDynamicSpecSourceTags().AddTag(OceanAdventureNavalTags::InputTag_Naval_Fire);
	UE_LOG(
		LogOceanAdventure,
		Display,
		TEXT("[HeavyWeaponOperate] Grant fire spec begin weapon=%s avatar=%s asc_owner=%s owner_role=%d avatar_role=%d tags=%s world=%.3f"),
		*GetNameSafe(Weapon), *GetNameSafe(GetAvatarActorFromActorInfo()),
		*GetNameSafe(AbilitySystem->GetOwner()), AbilitySystem->GetOwner() ? AbilitySystem->GetOwner()->GetLocalRole() : ROLE_None,
		GetAvatarActorFromActorInfo() ? GetAvatarActorFromActorInfo()->GetLocalRole() : ROLE_None,
		*FireSpec.GetDynamicSpecSourceTags().ToStringSimple(), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
	GrantedFireAbilityHandle = AbilitySystem->GiveAbility(FireSpec);

	UE_LOG(
		LogOceanAdventure,
		Display,
		TEXT("[HeavyWeaponOperate] Granted fire ability weapon=%s avatar=%s handle=%s"),
		*GetNameSafe(Weapon),
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		*GrantedFireAbilityHandle.ToString());
	if (const FGameplayAbilitySpec* GrantedSpec = AbilitySystem->FindAbilitySpecFromHandle(GrantedFireAbilityHandle))
	{
		UE_LOG(
			LogOceanAdventure,
			Display,
			TEXT("[HeavyWeaponOperate] Grant fire spec complete handle=%s ability=%s source=%s tags=%s active=%d input_pressed=%d world=%.3f"),
			*GrantedSpec->Handle.ToString(), *GetNameSafe(GrantedSpec->Ability.Get()),
			*GetNameSafe(GrantedSpec->SourceObject.Get()),
			*GrantedSpec->GetDynamicSpecSourceTags().ToStringSimple(), GrantedSpec->IsActive(),
			GrantedSpec->InputPressed, GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
	}
	return GrantedFireAbilityHandle.IsValid();
}

void UOceanAdventureGameplayAbility_OperateHeavyWeapon::RevokeFireAbility()
{
	if (!GrantedFireAbilityHandle.IsValid())
	{
		return;
	}

	if (ULyraAbilitySystemComponent* AbilitySystem = GetLyraAbilitySystemComponentFromActorInfo())
	{
		UE_LOG(
			LogOceanAdventure,
			Display,
			TEXT("[HeavyWeaponOperate] Revoked fire ability avatar=%s handle=%s"),
			*GetNameSafe(GetAvatarActorFromActorInfo()),
			*GrantedFireAbilityHandle.ToString());
		AbilitySystem->ClearAbility(GrantedFireAbilityHandle);
		UE_LOG(
			LogOceanAdventure,
			Display,
			TEXT("[HeavyWeaponOperate] Revoke fire spec complete handle=%s still_present=%d avatar=%s world=%.3f"),
			*GrantedFireAbilityHandle.ToString(),
			AbilitySystem->FindAbilitySpecFromHandle(GrantedFireAbilityHandle) != nullptr,
			*GetNameSafe(GetAvatarActorFromActorInfo()), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
	}
	GrantedFireAbilityHandle = FGameplayAbilitySpecHandle();
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
