// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_OperateHelm.h"

#include "AbilitySystemComponent.h"
#include "Character/LyraCharacterMovementComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Naval/NavalHelmComponent.h"
#include "Naval/NavalHelmStation.h"
#include "Naval/NavalMovementComponent.h"
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

	UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	UE_LOG(LogOceanAdventure, Display,
		TEXT("[NavalInputTrace] phase=operate-helm-post-super active=%d avatar=%s station=%s local=%d authority=%d movement_stopped_count=%d steering_count=%d"),
		IsActive(), *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(GetStationActor()),
		ActorInfo && ActorInfo->IsLocallyControlled(), HasAuthority(&ActivationInfo),
		AbilitySystem ? AbilitySystem->GetTagCount(TAG_Gameplay_MovementStopped) : -1,
		AbilitySystem ? AbilitySystem->GetTagCount(OceanAdventureNavalTags::Status_Naval_Steering) : -1);

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

		const AActor* VesselActor = GetStationVesselActor();
		const UNavalMovementComponent* Movement = VesselActor
			? VesselActor->FindComponentByClass<UNavalMovementComponent>()
			: nullptr;
		const ENavalMovementModel MovementModel = Movement
			? Movement->GetMovementModel()
			: ENavalMovementModel::Helm;
		HelmInput->EnableHelmInput(MovementModel);
		UE_LOG(LogOceanAdventure, Display,
			TEXT("[NavalInputTrace] phase=operate-helm-enable-input avatar=%s station=%s enabled=%d movement_stopped_count=%d steering_count=%d"),
			*GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(GetStationActor()),
			HelmInput->IsHelmInputEnabled(),
			AbilitySystem ? AbilitySystem->GetTagCount(TAG_Gameplay_MovementStopped) : -1,
			AbilitySystem ? AbilitySystem->GetTagCount(OceanAdventureNavalTags::Status_Naval_Steering) : -1);
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
	const UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	UE_LOG(LogOceanAdventure, Display,
		TEXT("[NavalInputTrace] phase=operate-helm-end-pre-disable avatar=%s station=%s cancelled=%d movement_stopped_count=%d steering_count=%d"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(GetStationActor()), bWasCancelled,
		AbilitySystem ? AbilitySystem->GetTagCount(TAG_Gameplay_MovementStopped) : -1,
		AbilitySystem ? AbilitySystem->GetTagCount(OceanAdventureNavalTags::Status_Naval_Steering) : -1);

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
		const UNavalMovementComponent* Movement = Helm->GetOwner()
			? Helm->GetOwner()->FindComponentByClass<UNavalMovementComponent>()
			: nullptr;
		const ENavalMovementModel MovementModel = Movement
			? Movement->GetMovementModel()
			: ENavalMovementModel::Helm;
		UE_LOG(LogOceanAdventure, Verbose,
			TEXT("[NavalInputTrace] phase=server-control-received avatar=%s station=%s vessel=%s operator=%s model=%d throttle=%.3f steer=%.3f move=(%.3f,%.3f) accepts_input=%d"),
			*GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(Station),
			*GetNameSafe(Helm->GetOwner()), *GetNameSafe(Helm->GetOperator()),
			static_cast<int32>(MovementModel), Data.GetThrottle(), Data.GetSteer(),
			Data.GetWorldMoveIntent().X, Data.GetWorldMoveIntent().Y, Helm->AcceptsControlInput());
		// The server-owned helm rejects samples from anyone except its current operator.
		if (MovementModel == ENavalMovementModel::DirectPlanar)
		{
			Helm->SetDirectControlIntent(
				GetAvatarActorFromActorInfo(),
				Data.GetWorldMoveIntent(),
				Data.AimLocation,
				Data.bHasFacingTarget);
		}
		else
		{
			Helm->SetControlIntent(
				GetAvatarActorFromActorInfo(), Data.GetThrottle(), Data.GetSteer());
		}
	}
	else
	{
		UE_LOG(LogOceanAdventure, Warning,
			TEXT("[NavalInputTrace] phase=server-control-received result=no-helm avatar=%s station=%s throttle=%.3f steer=%.3f"),
			*GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(Station),
			Data.GetThrottle(), Data.GetSteer());
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
		UE_LOG(LogOceanAdventure, Verbose,
			TEXT("[NavalInputTrace] phase=client-control-sample result=skipped avatar=%s station=%s vessel=%s input_component=%s input_enabled=%d"),
			*GetNameSafe(Avatar), *GetNameSafe(GetStationActor()), *GetNameSafe(VesselActor),
			*GetNameSafe(HelmInput), HelmInput && HelmInput->IsHelmInputEnabled());
		return false;
	}

	const UNavalMovementComponent* Movement =
		VesselActor->FindComponentByClass<UNavalMovementComponent>();
	const ENavalMovementModel MovementModel = Movement
		? Movement->GetMovementModel()
		: ENavalMovementModel::Helm;
	if (MovementModel == ENavalMovementModel::DirectPlanar)
	{
		const APawn* AvatarPawn = Cast<APawn>(Avatar);
		APlayerController* PlayerController = AvatarPawn
			? Cast<APlayerController>(AvatarPawn->GetController())
			: nullptr;
		if (!PlayerController)
		{
			return false;
		}

		// Axis2D is X=right, Y=forward. Transform it exactly like Lyra's native move path,
		// then send world XY because the server does not know the local camera/control yaw.
		const FVector2D LocalMove = HelmInput->GetDirectMoveInput().GetClampedToMaxSize(1.0f);
		const FRotator ControlYaw(0.0f, PlayerController->GetControlRotation().Yaw, 0.0f);
		const FVector WorldMove = ControlYaw.RotateVector(FVector(LocalMove.Y, LocalMove.X, 0.0f));

		FVector FacingTarget = FVector::ZeroVector;
		const bool bHasFacingTarget = UOceanAdventureNavalStatics::GetCursorAimLocation(
			PlayerController, FacingTarget);
		OutData.SetDirectControlIntent(
			FVector2D(WorldMove.X, WorldMove.Y), FacingTarget, bHasFacingTarget);
		return true;
	}

	OutData.SetControlIntent(HelmInput->GetThrottleInput(), HelmInput->GetSteerInput());
	UE_LOG(LogOceanAdventure, Verbose,
		TEXT("[NavalInputTrace] phase=client-control-sample result=built avatar=%s station=%s vessel=%s throttle=%.3f steer=%.3f"),
		*GetNameSafe(Avatar), *GetNameSafe(GetStationActor()), *GetNameSafe(VesselActor),
		HelmInput->GetThrottleInput(), HelmInput->GetSteerInput());
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

USceneComponent* UOceanAdventureGameplayAbility_OperateHelm::FindOperatorAttachmentPoint(
	AActor* Station) const
{
	if (!Station)
	{
		return nullptr;
	}

	TArray<USceneComponent*> SceneComponents;
	Station->GetComponents<USceneComponent>(SceneComponents);
	for (USceneComponent* SceneComponent : SceneComponents)
	{
		if (SceneComponent
			&& SceneComponent->ComponentHasTag(NavalHelmStation::GetOperatorPointComponentTag()))
		{
			return SceneComponent;
		}
	}

	UE_LOG(LogOceanAdventure, Error,
		TEXT("[NavalStation] Helm station has no tagged operator point station=%s required_tag=%s"),
		*GetNameSafe(Station), *NavalHelmStation::GetOperatorPointComponentTag().ToString());
	return nullptr;
}
