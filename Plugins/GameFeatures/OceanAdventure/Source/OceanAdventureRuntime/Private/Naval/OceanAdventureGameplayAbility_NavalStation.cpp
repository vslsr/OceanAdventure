// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_NavalStation.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureAbilityTask_NavalControl.h"
#include "Naval/OceanAdventureNavalMessages.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_NavalStation)

UOceanAdventureGameplayAbility_NavalStation::UOceanAdventureGameplayAbility_NavalStation(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UOceanAdventureGameplayAbility_NavalStation::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystem)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// The server waits on this delegate for every replicated sample; the local path calls the
	// same callback directly so both sides run identical code.
	OnTargetDataReadyHandle = AbilitySystem
		->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
		.AddUObject(this, &ThisClass::OnTargetDataReadyCallback);

	if (!ActorInfo->IsLocallyControlled())
	{
		AbilitySystem->CallReplicatedTargetDataDelegatesIfSet(
			Handle, ActivationInfo.GetActivationPredictionKey());
		return;
	}

	AActor* Station = FindStationInRange();
	if (!Station)
	{
		BroadcastStationFailure(NavalGameplayTags::Fail_TooFar, nullptr);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActiveStation = Station;

	FOceanAdventureNavalTargetData OccupyData;
	OccupyData.StationActor = Station;
	OccupyData.Request = ENavalStationRequest::Occupy;
	SendStationRequest(OccupyData);

	// Locally the character takes the station immediately. If the server refuses, it ends the
	// ability and the attachment unwinds in EndAbility.
	EnterStationPresentation(Station);

	ControlTask = UOceanAdventureAbilityTask_NavalControl::NavalControlTick(this, ControlSampleInterval);
	if (ControlTask)
	{
		ControlTask->OnControlSample.AddUObject(this, &ThisClass::HandleControlSample);
		ControlTask->ReadyForActivation();
	}

	StartLeaveInputWatch();
}

void UOceanAdventureGameplayAbility_NavalStation::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (OnTargetDataReadyHandle.IsValid() && ActorInfo)
	{
		if (UAbilitySystemComponent* AbilitySystem = ActorInfo->AbilitySystemComponent.Get())
		{
			AbilitySystem->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
				.Remove(OnTargetDataReadyHandle);
		}
		OnTargetDataReadyHandle.Reset();
	}

	if (ControlTask)
	{
		ControlTask->OnControlSample.RemoveAll(this);
		ControlTask->EndTask();
		ControlTask = nullptr;
	}

	if (AActor* Station = ActiveStation.Get())
	{
		if (HasAuthority(&ActivationInfo))
		{
			ServerReleaseStation(Station);
		}
		else if (ActorInfo && ActorInfo->IsLocallyControlled())
		{
			FOceanAdventureNavalTargetData ReleaseData;
			ReleaseData.StationActor = Station;
			ReleaseData.Request = ENavalStationRequest::Release;
			SendStationRequest(ReleaseData);
		}
	}

	if (bStationEntered)
	{
		LeaveStationPresentation();
		ApplyExitLock();
	}
	ActiveStation.Reset();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOceanAdventureGameplayAbility_NavalStation::SendStationRequest(const FOceanAdventureNavalTargetData& Data)
{
	UAbilitySystemComponent* AbilitySystem = CurrentActorInfo
		? CurrentActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	if (!AbilitySystem)
	{
		return;
	}

	FGameplayAbilityTargetDataHandle DataHandle;
	DataHandle.Add(new FOceanAdventureNavalTargetData(Data));
	OnTargetDataReadyCallback(DataHandle, FGameplayTag());
}

void UOceanAdventureGameplayAbility_NavalStation::OnTargetDataReadyCallback(
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
		for (int32 Index = 0; Index < LocalData.Num(); ++Index)
		{
			const FOceanAdventureNavalTargetData* Data =
				static_cast<const FOceanAdventureNavalTargetData*>(LocalData.Get(Index));
			AActor* Station = Data ? Data->StationActor.Get() : nullptr;
			if (!Data || !Station)
			{
				continue;
			}

			switch (Data->Request)
			{
			case ENavalStationRequest::Occupy:
				ActiveStation = Station;
				if (!ServerOccupyStation(Station))
				{
					// Refused on the server. The client already attached optimistically, so
					// ending here is what unwinds it.
					ActiveStation.Reset();
					EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
					return;
				}
				EnterStationPresentation(Station);
				break;

			case ENavalStationRequest::Release:
				ServerReleaseStation(Station);
				EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
				return;

			case ENavalStationRequest::Control:
				ServerApplyControl(Station, *Data);
				break;

			default:
				break;
			}
		}
	}

	AbilitySystem->ConsumeClientReplicatedTargetData(
		CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
}

void UOceanAdventureGameplayAbility_NavalStation::HandleControlSample(float /*DeltaTime*/)
{
	AActor* Station = ActiveStation.Get();
	if (!Station || !IsStationStillValid(Station))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		return;
	}

	FOceanAdventureNavalTargetData ControlData;
	ControlData.StationActor = Station;
	ControlData.Request = ENavalStationRequest::Control;
	if (BuildControlSample(ControlData))
	{
		SendStationRequest(ControlData);
	}
}

void UOceanAdventureGameplayAbility_NavalStation::StartLeaveInputWatch()
{
	// Lyra re-sends the bound input to an already-active ability, so waiting on the same input
	// turns "press to use" into "press again to leave" without a second binding.
	UAbilityTask_WaitInputPress* WaitTask = UAbilityTask_WaitInputPress::WaitInputPress(this, /*bTestInitialState=*/false);
	if (!WaitTask)
	{
		return;
	}

	WaitTask->OnPress.AddDynamic(this, &ThisClass::OnLeaveInputPressed);
	WaitTask->ReadyForActivation();
}

void UOceanAdventureGameplayAbility_NavalStation::OnLeaveInputPressed(float /*TimeWaited*/)
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UOceanAdventureGameplayAbility_NavalStation::EnterStationPresentation(AActor* Station)
{
	if (bStationEntered || !Station)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return;
	}

	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		// The character is standing at the wheel, not walking. Movement input is taken over by
		// the control samples below rather than being fed to the legs.
		Movement->SetMovementMode(MOVE_None);
	}
	Character->AttachToActor(Station, FAttachmentTransformRules::KeepWorldTransform);

	if (UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo())
	{
		const FGameplayTag StatusTag = GetStationStatusTag();
		if (StatusTag.IsValid())
		{
			AbilitySystem->AddLooseGameplayTag(StatusTag);
		}
	}

	bStationEntered = true;
}

void UOceanAdventureGameplayAbility_NavalStation::LeaveStationPresentation()
{
	bStationEntered = false;

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return;
	}

	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
	}

	if (UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo())
	{
		const FGameplayTag StatusTag = GetStationStatusTag();
		if (StatusTag.IsValid())
		{
			AbilitySystem->RemoveLooseGameplayTag(StatusTag);
		}
	}
}

void UOceanAdventureGameplayAbility_NavalStation::ApplyExitLock()
{
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo();
	UWorld* World = GetWorld();
	if (!AbilitySystem || !World || StationExitLockSeconds <= 0.0f)
	{
		return;
	}

	// Stepping off a gun and shooting has to cost something, or the gun would have no
	// downside at close range at all.
	AbilitySystem->AddLooseGameplayTag(OceanAdventureNavalTags::Status_Naval_StationExitLock);

	TWeakObjectPtr<UAbilitySystemComponent> WeakAbilitySystem(AbilitySystem);
	FTimerHandle ExitLockTimer;
	World->GetTimerManager().SetTimer(
		ExitLockTimer,
		FTimerDelegate::CreateWeakLambda(
			AbilitySystem,
			[WeakAbilitySystem]()
			{
				if (UAbilitySystemComponent* AbilitySystemComponent = WeakAbilitySystem.Get())
				{
					AbilitySystemComponent->RemoveLooseGameplayTag(
						OceanAdventureNavalTags::Status_Naval_StationExitLock);
				}
			}),
		StationExitLockSeconds,
		false);
}

AActor* UOceanAdventureGameplayAbility_NavalStation::GetStationVesselActor() const
{
	const UNavalVesselComponent* Vessel = UNavalVesselComponent::FindVessel(ActiveStation.Get());
	return Vessel ? Vessel->GetOwner() : nullptr;
}

void UOceanAdventureGameplayAbility_NavalStation::BroadcastStationFailure(
	FGameplayTag FailReason, AActor* Station) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FOceanAdventureNavalStationFailedMessage Message;
	Message.FailReason = FailReason;
	Message.StationActor = Station;
	Message.Instigator = GetAvatarActorFromActorInfo();
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(
		OceanAdventureNavalTags::Message_Naval_StationFailed, Message);
}

AActor* UOceanAdventureGameplayAbility_NavalStation::FindStationInRange() const
{
	return nullptr;
}

bool UOceanAdventureGameplayAbility_NavalStation::ServerOccupyStation(AActor* /*Station*/)
{
	return false;
}

void UOceanAdventureGameplayAbility_NavalStation::ServerReleaseStation(AActor* /*Station*/)
{
}

void UOceanAdventureGameplayAbility_NavalStation::ServerApplyControl(
	AActor* /*Station*/, const FOceanAdventureNavalTargetData& /*Data*/)
{
}

bool UOceanAdventureGameplayAbility_NavalStation::BuildControlSample(
	FOceanAdventureNavalTargetData& /*OutData*/) const
{
	return false;
}

FGameplayTag UOceanAdventureGameplayAbility_NavalStation::GetStationStatusTag() const
{
	return FGameplayTag();
}

bool UOceanAdventureGameplayAbility_NavalStation::IsStationStillValid(AActor* Station) const
{
	return IsValid(Station);
}
