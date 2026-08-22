// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_EmergencyRepair.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalRegistrySubsystem.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureNavalMessages.h"
#include "Naval/OceanAdventureNavalStatics.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_EmergencyRepair)

UOceanAdventureGameplayAbility_EmergencyRepair::UOceanAdventureGameplayAbility_EmergencyRepair(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

UNavalVesselComponent* UOceanAdventureGameplayAbility_EmergencyRepair::FindRepairableVessel() const
{
	APawn* Pawn = Cast<APawn>(GetAvatarActorFromActorInfo());
	if (!Pawn)
	{
		return nullptr;
	}

	if (UNavalVesselComponent* Standing = UOceanAdventureNavalStatics::FindVesselUnderPawn(Pawn))
	{
		return Standing;
	}

	// Swimming beside a hull still counts; the vessel's own range check has the final say.
	if (const UNavalRegistrySubsystem* Registry = UNavalRegistrySubsystem::Get(this))
	{
		UNavalVesselComponent* Nearest = Registry->FindNearestVessel(Pawn->GetActorLocation());
		const AActor* NearestActor = Nearest ? Nearest->GetOwner() : nullptr;
		if (NearestActor
			&& FVector::DistSquared(NearestActor->GetActorLocation(), Pawn->GetActorLocation())
				<= FMath::Square(static_cast<double>(VesselSearchRadius)))
		{
			return Nearest;
		}
	}

	return nullptr;
}

void UOceanAdventureGameplayAbility_EmergencyRepair::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bCommitSent = false;
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystem)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	OnTargetDataReadyHandle = AbilitySystem
		->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
		.AddUObject(this, &ThisClass::OnTargetDataReadyCallback);

	if (!ActorInfo->IsLocallyControlled())
	{
		AbilitySystem->CallReplicatedTargetDataDelegatesIfSet(
			Handle, ActivationInfo.GetActivationPredictionKey());
		return;
	}

	UNavalVesselComponent* Vessel = FindRepairableVessel();
	FGameplayTag FailReason;
	if (!Vessel || !Vessel->CanBeginEmergencyRepair(GetAvatarActorFromActorInfo(), FailReason))
	{
		BroadcastFailure(
			FailReason.IsValid() ? FailReason : NavalGameplayTags::Fail_NotOperational,
			Vessel ? Vessel->GetOwner() : nullptr);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	RepairTargetActor = Vessel->GetOwner();
	AbilitySystem->AddLooseGameplayTag(OceanAdventureNavalTags::Status_Naval_Repairing);

	UAbilityTask_WaitDelay* ChannelTask =
		UAbilityTask_WaitDelay::WaitDelay(this, Vessel->GetEmergencyRepairSeconds());
	if (!ChannelTask)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ChannelTask->OnFinish.AddDynamic(this, &ThisClass::OnRepairChannelFinished);
	ChannelTask->ReadyForActivation();
}

void UOceanAdventureGameplayAbility_EmergencyRepair::OnRepairChannelFinished()
{
	AActor* VesselActor = RepairTargetActor.Get();
	if (!VesselActor || bCommitSent)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	bCommitSent = true;

	FOceanAdventureNavalTargetData CommitData;
	CommitData.StationActor = VesselActor;
	CommitData.Request = ENavalStationRequest::CommitRepair;

	FGameplayAbilityTargetDataHandle DataHandle;
	DataHandle.Add(new FOceanAdventureNavalTargetData(CommitData));
	OnTargetDataReadyCallback(DataHandle, FGameplayTag());
}

void UOceanAdventureGameplayAbility_EmergencyRepair::OnTargetDataReadyCallback(
	const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag)
{
	UAbilitySystemComponent* AbilitySystem = CurrentActorInfo
		? CurrentActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	if (!AbilitySystem)
	{
		return;
	}

	FScopedPredictionWindow ScopedPrediction(
		AbilitySystem, CurrentActivationInfo.GetActivationPredictionKey());

	FGameplayAbilityTargetDataHandle LocalData(
		MoveTemp(const_cast<FGameplayAbilityTargetDataHandle&>(InData)));

	if (CurrentActorInfo->IsLocallyControlled() && !CurrentActorInfo->IsNetAuthority())
	{
		AbilitySystem->CallServerSetReplicatedTargetData(
			CurrentSpecHandle,
			CurrentActivationInfo.GetActivationPredictionKey(),
			LocalData,
			ApplicationTag,
			AbilitySystem->ScopedPredictionKey);
	}

	if (HasAuthority(&CurrentActivationInfo) && LocalData.Num() > 0)
	{
		const FOceanAdventureNavalTargetData* Data =
			static_cast<const FOceanAdventureNavalTargetData*>(LocalData.Get(0));
		AActor* VesselActor = Data ? Data->StationActor.Get() : nullptr;
		UNavalVesselComponent* Vessel = VesselActor
			? VesselActor->FindComponentByClass<UNavalVesselComponent>()
			: nullptr;

		// The channel finished on the client; whether it counts is decided here, against the
		// world as the server sees it now.
		if (!Vessel || !Vessel->CommitEmergencyRepair(GetAvatarActorFromActorInfo()))
		{
			UE_LOG(
				LogOceanAdventure,
				Verbose,
				TEXT("[NavalRepair] Server refused emergency repair vessel=%s avatar=%s"),
				*GetNameSafe(VesselActor),
				*GetNameSafe(GetAvatarActorFromActorInfo()));
		}
	}

	AbilitySystem->ConsumeClientReplicatedTargetData(
		CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UOceanAdventureGameplayAbility_EmergencyRepair::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (ActorInfo)
	{
		if (UAbilitySystemComponent* AbilitySystem = ActorInfo->AbilitySystemComponent.Get())
		{
			AbilitySystem->RemoveLooseGameplayTag(OceanAdventureNavalTags::Status_Naval_Repairing);
			if (OnTargetDataReadyHandle.IsValid())
			{
				AbilitySystem->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
					.Remove(OnTargetDataReadyHandle);
			}
		}
	}
	OnTargetDataReadyHandle.Reset();
	RepairTargetActor.Reset();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOceanAdventureGameplayAbility_EmergencyRepair::BroadcastFailure(
	FGameplayTag FailReason, AActor* VesselActor) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FOceanAdventureNavalStationFailedMessage Message;
	Message.FailReason = FailReason;
	Message.StationActor = VesselActor;
	Message.Instigator = GetAvatarActorFromActorInfo();
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(
		OceanAdventureNavalTags::Message_Naval_StationFailed, Message);
}
