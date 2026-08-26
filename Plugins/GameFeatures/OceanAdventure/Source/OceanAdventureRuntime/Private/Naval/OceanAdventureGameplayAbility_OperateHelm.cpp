// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_OperateHelm.h"

#include "Naval/NavalHelmActor.h"
#include "Naval/NavalHelmComponent.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureHelmInputComponent.h"
#include "Naval/OceanAdventureNavalStatics.h"
#include "Naval/OceanAdventureNavalTags.h"

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
	// only the target-data samples and never installs a client input mapping.
	if (IsActive() && ActorInfo && ActorInfo->IsLocallyControlled())
	{
		if (UOceanAdventureHelmInputComponent* HelmInput =
			GetAvatarActorFromActorInfo()
				? GetAvatarActorFromActorInfo()->FindComponentByClass<UOceanAdventureHelmInputComponent>()
				: nullptr)
		{
			HelmInput->EnableHelmInput();
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
	// Remove the high-priority context before the base class restores walking. This guarantees
	// W/A/S/D are immediately handed back to IMC_OceanAdventure_Base on every exit path.
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
	const ANavalHelmActor* HelmActor = Cast<ANavalHelmActor>(Station);
	return HelmActor ? HelmActor->GetHelmComponent() : nullptr;
}

AActor* UOceanAdventureGameplayAbility_OperateHelm::FindStationInRange() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return nullptr;
	}

	return UOceanAdventureNavalStatics::FindNearestStationActor(
		this, Avatar->GetActorLocation(), StationSearchRadius, ANavalHelmActor::StaticClass());
}

bool UOceanAdventureGameplayAbility_OperateHelm::ServerOccupyStation(AActor* Station)
{
	UNavalHelmComponent* Helm = ResolveHelm(Station);
	return Helm && Helm->TryOccupy(GetAvatarActorFromActorInfo());
}

void UOceanAdventureGameplayAbility_OperateHelm::ServerReleaseStation(AActor* Station)
{
	if (UNavalHelmComponent* Helm = ResolveHelm(Station))
	{
		Helm->ReleaseHelm(GetAvatarActorFromActorInfo());
	}
}

void UOceanAdventureGameplayAbility_OperateHelm::ServerApplyControl(
	AActor* Station, const FOceanAdventureNavalTargetData& Data)
{
	if (UNavalHelmComponent* Helm = ResolveHelm(Station))
	{
		// The helm itself checks that this actor is the one it believes is steering.
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

	// The dedicated Axis1D actions are already ship-relative: W/S produce signed throttle and
	// A/D produce signed torque. IMC_OceanHelm at priority 2 prevents the top-down move actions
	// from seeing the same physical keys while this ability is active.
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

	// Null covers the window before the server's occupancy has replicated back; a different
	// operator means the wheel was taken -- by a capture, or by a team-mate on the server.
	const AActor* CurrentOperator = Helm->GetOperator();
	return CurrentOperator == nullptr || CurrentOperator == GetAvatarActorFromActorInfo();
}

bool UOceanAdventureGameplayAbility_OperateHelm::GetOperatorTransform(
	AActor* Station, FTransform& OutTransform) const
{
	const ANavalHelmActor* HelmActor = Cast<ANavalHelmActor>(Station);
	if (!HelmActor)
	{
		return false;
	}

	// Standing behind the wheel and facing the bow, so W/S read as ahead/astern for the
	// player exactly as they do for the hull.
	OutTransform = HelmActor->GetOperatorTransform();
	return true;
}
