// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_OperateHelm.h"

#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "Naval/NavalHelmComponent.h"
#include "Naval/NavalHelmStation.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureGameplayAbility_DriveHelm.h"
#include "Naval/OceanAdventureNavalStatics.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_OperateHelm)

UOceanAdventureGameplayAbility_OperateHelm::UOceanAdventureGameplayAbility_OperateHelm(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UOceanAdventureGameplayAbility_OperateHelm::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (ActorInfo && ActorInfo->IsLocallyControlled())
	{
		if (ULyraAbilitySystemComponent* AbilitySystem =
			Cast<ULyraAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()))
		{
			// Do not wait one round trip for the server's spec removal before handing WASD back
			// to TopDown movement. The server still owns the actual ClearAbility below.
			AbilitySystem->CancelAbilitiesByFunc(
				[](const ULyraGameplayAbility* Ability, FGameplayAbilitySpecHandle)
				{
					return Ability && Ability->IsA<UOceanAdventureGameplayAbility_DriveHelm>();
				},
				/*bReplicateCancelAbility=*/true);
		}
	}

	// The station pointer may already be invalid after destruction. Revoke independently so a
	// stale DriveHelm spec can never keep consuming W/A/S/D after the player leaves.
	if (HasAuthority(&ActivationInfo))
	{
		RevokeDriveAbility();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

UNavalHelmComponent* UOceanAdventureGameplayAbility_OperateHelm::ResolveHelm(AActor* Station)
{
	const INavalHelmStation* HelmStation = Station ? Cast<INavalHelmStation>(Station) : nullptr;
	return HelmStation ? HelmStation->GetHelmComponent() : nullptr;
}

AActor* UOceanAdventureGameplayAbility_OperateHelm::FindStationInRange() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return nullptr;
	}

	return UOceanAdventureNavalStatics::FindNearestUsableHelmStation(
		this, Avatar->GetActorLocation(), StationSearchRadius, Avatar);
}

bool UOceanAdventureGameplayAbility_OperateHelm::ServerOccupyStation(AActor* Station)
{
	INavalHelmStation* HelmStation = Station ? Cast<INavalHelmStation>(Station) : nullptr;
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!HelmStation || !HelmStation->TryOccupy(Avatar))
	{
		return false;
	}

	if (!GrantDriveAbility(Station))
	{
		HelmStation->ReleaseOperator(Avatar);
		return false;
	}
	return true;
}

void UOceanAdventureGameplayAbility_OperateHelm::ServerReleaseStation(AActor* Station)
{
	RevokeDriveAbility();
	if (INavalHelmStation* HelmStation = Station ? Cast<INavalHelmStation>(Station) : nullptr)
	{
		HelmStation->ReleaseOperator(GetAvatarActorFromActorInfo());
	}
}

bool UOceanAdventureGameplayAbility_OperateHelm::GrantDriveAbility(AActor* Station)
{
	ULyraAbilitySystemComponent* AbilitySystem = GetLyraAbilitySystemComponentFromActorInfo();
	if (!HasAuthority(&CurrentActivationInfo) || !AbilitySystem || !Station)
	{
		return false;
	}

	RevokeDriveAbility();
	UOceanAdventureGameplayAbility_DriveHelm* DriveAbilityCDO =
		UOceanAdventureGameplayAbility_DriveHelm::StaticClass()
			->GetDefaultObject<UOceanAdventureGameplayAbility_DriveHelm>();
	FGameplayAbilitySpec DriveSpec(DriveAbilityCDO, 1);
	DriveSpec.SourceObject = Station;
	DriveSpec.GetDynamicSpecSourceTags().AddTag(
		OceanAdventureNavalTags::InputTag_Naval_Helm_Throttle);
	DriveSpec.GetDynamicSpecSourceTags().AddTag(
		OceanAdventureNavalTags::InputTag_Naval_Helm_Steer);
	GrantedDriveAbilityHandle = AbilitySystem->GiveAbility(DriveSpec);

	UE_LOG(LogOceanAdventure, Display,
		TEXT("[Helm] Granted DriveHelm station=%s avatar=%s handle=%s"),
		*GetNameSafe(Station), *GetNameSafe(GetAvatarActorFromActorInfo()),
		*GrantedDriveAbilityHandle.ToString());
	return GrantedDriveAbilityHandle.IsValid();
}

void UOceanAdventureGameplayAbility_OperateHelm::RevokeDriveAbility()
{
	if (!GrantedDriveAbilityHandle.IsValid())
	{
		return;
	}
	if (ULyraAbilitySystemComponent* AbilitySystem = GetLyraAbilitySystemComponentFromActorInfo())
	{
		UE_LOG(LogOceanAdventure, Display,
			TEXT("[Helm] Revoked DriveHelm avatar=%s handle=%s"),
			*GetNameSafe(GetAvatarActorFromActorInfo()),
			*GrantedDriveAbilityHandle.ToString());
		AbilitySystem->ClearAbility(GrantedDriveAbilityHandle);
	}
	GrantedDriveAbilityHandle = FGameplayAbilitySpecHandle();
}

FGameplayTag UOceanAdventureGameplayAbility_OperateHelm::GetStationStatusTag() const
{
	return OceanAdventureNavalTags::Status_Naval_Steering;
}

bool UOceanAdventureGameplayAbility_OperateHelm::IsStationStillValid(AActor* Station) const
{
	const UNavalHelmComponent* Helm = ResolveHelm(Station);
	if (!Helm)
	{
		return false;
	}

	const UNavalVesselComponent* Vessel = Helm->GetVessel();
	if (Vessel && Vessel->GetVesselState() == ENavalVesselState::Wreck)
	{
		return false;
	}

	// Null covers the window before the server's occupancy has replicated back; a different
	// operator means the wheel was taken -- by a capture, or by a team-mate on the server.
	const AActor* CurrentOperator = Helm->GetOperator();
	const AActor* CurrentStation = Helm->GetActiveStation();
	return (CurrentOperator == nullptr || CurrentOperator == GetAvatarActorFromActorInfo())
		&& (CurrentStation == nullptr || CurrentStation == Station);
}

bool UOceanAdventureGameplayAbility_OperateHelm::GetOperatorTransform(
	AActor* Station, FTransform& OutTransform) const
{
	const INavalHelmStation* HelmStation = Station ? Cast<INavalHelmStation>(Station) : nullptr;
	if (!HelmStation)
	{
		return false;
	}

	// Standing behind the wheel and facing the bow, so W/S read as ahead/astern for the
	// player exactly as they do for the hull.
	OutTransform = HelmStation->GetOperatorTransform();
	return true;
}
