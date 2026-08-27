// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_NavalStation.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Character/LyraCharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "Build/OceanAdventureBuildTags.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureAbilityTask_NavalControl.h"
#include "Naval/OceanAdventureNavalMessages.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"
#include "System/LyraAssetManager.h"
#include "System/LyraGameData.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_NavalStation)

UOceanAdventureGameplayAbility_NavalStation::UOceanAdventureGameplayAbility_NavalStation(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// Only one cursor-owning station/building mode may run at a time.  These are activation
	// requirements rather than ad-hoc checks so prediction and the server use the same gate.
	ActivationBlockedTags.AddTag(OceanAdventureNavalTags::Status_Naval_Steering);
	ActivationBlockedTags.AddTag(OceanAdventureNavalTags::Status_Naval_OperatingHeavyWeapon);
	ActivationBlockedTags.AddTag(OceanAdventureNavalTags::Status_Naval_Repairing);
	ActivationBlockedTags.AddTag(OceanAdventureBuildTags::Status_Build_Active);
}

void UOceanAdventureGameplayAbility_NavalStation::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	UE_LOG(
		LogOceanAdventure,
		Display,
		TEXT("[NavalStation] Activate ability=%s avatar=%s local=%d authority=%d search_radius=%.1f"),
		*GetNameSafe(this),
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		ActorInfo && ActorInfo->IsLocallyControlled(),
		HasAuthority(&ActivationInfo),
		StationSearchRadius);

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
		const AActor* Avatar = GetAvatarActorFromActorInfo();
		const FString OriginText = Avatar ? Avatar->GetActorLocation().ToCompactString() : TEXT("None");
		UE_LOG(LogOceanAdventure, Warning,
			TEXT("[NavalStation] No station found ability=%s avatar=%s origin=%s radius=%.1f"),
			*GetNameSafe(this), *GetNameSafe(Avatar), *OriginText,
			StationSearchRadius);
		BroadcastStationFailure(NavalGameplayTags::Fail_TooFar, nullptr);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActiveStation = Station;
	UE_LOG(LogOceanAdventure, Display,
		TEXT("[NavalStation] Local station selected ability=%s station=%s distance=%.1f"),
		*GetNameSafe(this), *GetNameSafe(Station),
		GetAvatarActorFromActorInfo()
			? FVector::Dist(GetAvatarActorFromActorInfo()->GetActorLocation(), Station->GetActorLocation()) : -1.0f);

	FOceanAdventureNavalTargetData OccupyData;
	OccupyData.StationActor = Station;
	OccupyData.Request = ENavalStationRequest::Occupy;
	SendStationRequest(OccupyData);
	if (!IsActive())
	{
		// On a listen server the target-data callback can reject synchronously and end this
		// ability before presentation starts.  Do not attach a pawn after that rejection.
		return;
	}

	// Locally the character takes the station immediately. If the server refuses, it ends the
	// ability and the attachment unwinds in EndAbility.
	EnterStationPresentation(Station);
	if (!bStationEntered)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

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
	UE_LOG(LogOceanAdventure, Display,
		TEXT("[NavalStation] End ability=%s avatar=%s station=%s entered=%d cancelled=%d local=%d authority=%d"),
		*GetNameSafe(this), *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(ActiveStation.Get()),
		bStationEntered, bWasCancelled, ActorInfo && ActorInfo->IsLocallyControlled(), HasAuthority(&ActivationInfo));
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
		UE_LOG(LogOceanAdventure, Verbose,
			TEXT("[NavalStation] Send TargetData request=%d station=%s avatar=%s"),
			static_cast<int32>(LocalData.Num() > 0
				? static_cast<const FOceanAdventureNavalTargetData*>(LocalData.Get(0))->Request
				: ENavalStationRequest::Release),
			*GetNameSafe(LocalData.Num() > 0
				? static_cast<const FOceanAdventureNavalTargetData*>(LocalData.Get(0))->StationActor.Get() : nullptr),
			*GetNameSafe(GetAvatarActorFromActorInfo()));
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
				UE_LOG(LogOceanAdventure, Display,
					TEXT("[NavalStation] Server occupy request ability=%s station=%s avatar=%s"),
					*GetNameSafe(this), *GetNameSafe(Station), *GetNameSafe(GetAvatarActorFromActorInfo()));
				if (!ServerOccupyStation(Station))
				{
					UE_LOG(LogOceanAdventure, Warning,
						TEXT("[NavalStation] Server occupy refused ability=%s station=%s avatar=%s"),
						*GetNameSafe(this), *GetNameSafe(Station), *GetNameSafe(GetAvatarActorFromActorInfo()));
					// Refused on the server. The client already attached optimistically, so
					// ending here is what unwinds it.
					ActiveStation.Reset();
					EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
					return;
				}
				EnterStationPresentation(Station);
				if (!bStationEntered)
				{
					UE_LOG(LogOceanAdventure, Error,
						TEXT("[NavalStation] Station lock failed ability=%s station=%s avatar=%s"),
						*GetNameSafe(this), *GetNameSafe(Station),
						*GetNameSafe(GetAvatarActorFromActorInfo()));
					EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
					return;
				}
				UE_LOG(LogOceanAdventure, Display,
					TEXT("[NavalStation] Server occupy accepted ability=%s station=%s avatar=%s"),
					*GetNameSafe(this), *GetNameSafe(Station), *GetNameSafe(GetAvatarActorFromActorInfo()));
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

	// CharacterMovement keeps its real mode (walking, falling, swimming, ...). The replicated
	// GAS lock below makes Lyra return zero speed/rotation without corrupting that state.
	if (!ApplyStationLock())
	{
		return;
	}

	FTransform OperatorTransform;
	if (GetOperatorTransform(Station, OperatorTransform))
	{
		Character->SetActorLocationAndRotation(
			OperatorTransform.GetLocation(),
			OperatorTransform.Rotator(),
			/*bSweep=*/false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}
	Character->AttachToActor(Station, FAttachmentTransformRules::KeepWorldTransform);

	const FGameplayTag StatusTag = GetStationStatusTag();
	UE_LOG(LogOceanAdventure, Display,
		TEXT("[NavalStation] Presentation entered avatar=%s station=%s status=%s attached_to=%s"),
		*GetNameSafe(Character), *GetNameSafe(Station), *StatusTag.ToString(),
		*GetNameSafe(Character->GetAttachParentActor()));

	bStationEntered = true;
}

void UOceanAdventureGameplayAbility_NavalStation::LeaveStationPresentation()
{
	bStationEntered = false;

	// The handle remembers the PlayerState ASC even if its pawn/avatar has already gone away.
	// Removing the effect clears both MovementStopped and the station status atomically.
	RemoveStationLock();

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return;
	}

	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
}

bool UOceanAdventureGameplayAbility_NavalStation::ApplyStationLock()
{
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo();
	if (!AbilitySystem)
	{
		return false;
	}

	const TSubclassOf<UGameplayEffect> DynamicTagEffect =
		ULyraAssetManager::GetSubclass(ULyraGameData::Get().DynamicTagGameplayEffect);
	if (!DynamicTagEffect)
	{
		UE_LOG(LogOceanAdventure, Error,
			TEXT("[NavalStation] Lyra DynamicTagGameplayEffect is not configured"));
		return false;
	}

	// Explicitly associate the predicted client effect and the authoritative server effect
	// with this ability activation so GAS reconciles them instead of leaving two lock specs.
	FScopedPredictionWindow ScopedPrediction(
		AbilitySystem, CurrentActivationInfo.GetActivationPredictionKey());
	FGameplayEffectSpecHandle SpecHandle =
		AbilitySystem->MakeOutgoingSpec(DynamicTagEffect, 1.0f, AbilitySystem->MakeEffectContext());
	FGameplayEffectSpec* Spec = SpecHandle.Data.Get();
	if (!Spec)
	{
		return false;
	}

	Spec->DynamicGrantedTags.AddTag(TAG_Gameplay_MovementStopped);
	const FGameplayTag StatusTag = GetStationStatusTag();
	if (StatusTag.IsValid())
	{
		Spec->DynamicGrantedTags.AddTag(StatusTag);
	}

	StationLockHandle = AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec);
	if (!StationLockHandle.IsValid())
	{
		return false;
	}

	StationLockAbilitySystem = AbilitySystem;
	return true;
}

void UOceanAdventureGameplayAbility_NavalStation::RemoveStationLock()
{
	if (StationLockHandle.IsValid())
	{
		if (UAbilitySystemComponent* AbilitySystem = StationLockAbilitySystem.Get())
		{
			AbilitySystem->RemoveActiveGameplayEffect(StationLockHandle);
		}
	}

	StationLockHandle = FActiveGameplayEffectHandle();
	StationLockAbilitySystem.Reset();
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

bool UOceanAdventureGameplayAbility_NavalStation::GetOperatorTransform(
	AActor* /*Station*/, FTransform& /*OutTransform*/) const
{
	return false;
}
