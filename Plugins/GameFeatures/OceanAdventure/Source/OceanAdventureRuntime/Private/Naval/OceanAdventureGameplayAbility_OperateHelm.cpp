// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_OperateHelm.h"

#include "GameFramework/Pawn.h"
#include "Naval/NavalHelmActor.h"
#include "Naval/NavalHelmComponent.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureNavalStatics.h"
#include "Naval/OceanAdventureNavalTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_OperateHelm)

UOceanAdventureGameplayAbility_OperateHelm::UOceanAdventureGameplayAbility_OperateHelm(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
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
	APawn* Pawn = Cast<APawn>(GetAvatarActorFromActorInfo());
	const AActor* VesselActor = GetStationVesselActor();
	if (!Pawn || !VesselActor)
	{
		return false;
	}

	// Consuming the vector is what takes the input over: with the character in MOVE_None
	// nothing else will read it, and leaving it to accumulate would make the first step after
	// stepping off the wheel lurch.
	const FVector MovementInput = Pawn->ConsumeMovementInputVector();

	// Ship-relative, so pushing towards where the bow points is throttle and pushing across it
	// is helm -- the same relationship whichever way the camera is looking.
	const float Throttle = static_cast<float>(FVector::DotProduct(MovementInput, VesselActor->GetActorForwardVector()));
	const float Steer = static_cast<float>(FVector::DotProduct(MovementInput, VesselActor->GetActorRightVector()));
	OutData.SetControlIntent(Throttle, Steer);
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
