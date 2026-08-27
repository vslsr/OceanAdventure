// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_OperateHelm.h"

#include "Naval/NavalHelmComponent.h"
#include "Naval/NavalHelmStation.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureHelmInputComponent.h"
#include "Naval/OceanAdventureNavalStatics.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_OperateHelm)

UOceanAdventureGameplayAbility_OperateHelm::UOceanAdventureGameplayAbility_OperateHelm(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UOceanAdventureGameplayAbility_OperateHelm::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// Only the local predicted ability owns the player's mapping context. The server receives
	// target-data samples and never installs a client input mapping.
	if (IsActive() && ActorInfo && ActorInfo->IsLocallyControlled())
	{
		UOceanAdventureHelmInputComponent* HelmInput =
			GetAvatarActorFromActorInfo()
				? GetAvatarActorFromActorInfo()->FindComponentByClass<UOceanAdventureHelmInputComponent>()
				: nullptr;
		if (!HelmInput)
		{
			UE_LOG(LogOceanAdventure, Error,
				TEXT("[Helm] Avatar %s has no OceanAdventureHelmInputComponent"),
				*GetNameSafe(GetAvatarActorFromActorInfo()));
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		HelmInput->EnableHelmInput();
		if (!HelmInput->IsHelmInputEnabled())
		{
			UE_LOG(LogOceanAdventure, Error,
				TEXT("[Helm] Failed to enable tagged helm input for avatar %s"),
				*GetNameSafe(GetAvatarActorFromActorInfo()));
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		}
	}
}

void UOceanAdventureGameplayAbility_OperateHelm::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// Pop the high-priority context before the base class removes the station lock. W/A/S/D
	// then return immediately to the native TopDown + CharacterMovement path.
	if (ActorInfo && ActorInfo->IsLocallyControlled())
	{
		if (UOceanAdventureHelmInputComponent* HelmInput =
			GetAvatarActorFromActorInfo()
				? GetAvatarActorFromActorInfo()->FindComponentByClass<UOceanAdventureHelmInputComponent>()
				: nullptr)
		{
			HelmInput->DisableHelmInput();
		}
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
	return HelmStation && HelmStation->TryOccupy(GetAvatarActorFromActorInfo());
}

void UOceanAdventureGameplayAbility_OperateHelm::ServerReleaseStation(AActor* Station)
{
	if (INavalHelmStation* HelmStation = Station ? Cast<INavalHelmStation>(Station) : nullptr)
	{
		HelmStation->ReleaseOperator(GetAvatarActorFromActorInfo());
	}
}

void UOceanAdventureGameplayAbility_OperateHelm::ServerApplyControl(
	AActor* Station, const FOceanAdventureNavalTargetData& Data)
{
	if (UNavalHelmComponent* Helm = ResolveHelm(Station))
	{
		// The server-owned helm rejects samples from anyone except its current operator.
		Helm->SetControlIntent(GetAvatarActorFromActorInfo(), Data.GetThrottle(), Data.GetSteer());
	}
}

bool UOceanAdventureGameplayAbility_OperateHelm::BuildControlSample(
	FOceanAdventureNavalTargetData& OutData) const
{
	const AActor* VesselActor = GetStationVesselActor();
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	const UOceanAdventureHelmInputComponent* HelmInput = Avatar
		? Avatar->FindComponentByClass<UOceanAdventureHelmInputComponent>()
		: nullptr;
	if (!VesselActor || !HelmInput || !HelmInput->IsHelmInputEnabled())
	{
		return false;
	}

	OutData.SetControlIntent(HelmInput->GetThrottleInput(), HelmInput->GetSteerInput());
	return true;
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

	OutTransform = HelmStation->GetOperatorTransform();
	return true;
}
