// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_NavalStation.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Character/LyraCharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "Build/OceanAdventureBuildTags.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureAbilityTask_NavalControl.h"
#include "Naval/OceanAdventureGameplayEffect_NavalStationExitLock.h"
#include "Naval/OceanAdventureGameplayEffect_NavalStationLock.h"
#include "Naval/OceanAdventureNavalMessages.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"

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
		UE_LOG(LogOceanAdventure, Error,
			TEXT("[NavalStation] Local station lock failed ability=%s station=%s avatar=%s"),
			*GetNameSafe(this), *GetNameSafe(Station),
			*GetNameSafe(GetAvatarActorFromActorInfo()));
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

	// Diagnostic only: the operator root should remain exactly relative-zero to the authored
	// attachment point. Log only bad states; a normally attached pawn produces no sample noise.
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	USceneComponent* CharacterRoot = Character ? Character->GetRootComponent() : nullptr;
	USceneComponent* ExpectedAttachmentPoint = FindOperatorAttachmentPoint(Station);
	if (Character && CharacterRoot && ExpectedAttachmentPoint)
	{
		const FTransform RelativeToPoint = CharacterRoot->GetComponentTransform().GetRelativeTransform(
			ExpectedAttachmentPoint->GetComponentTransform());
		const float RotationErrorDegrees = FMath::RadiansToDegrees(
			RelativeToPoint.GetRotation().AngularDistance(FQuat::Identity));
		const bool bAttachedToExpectedPoint =
			CharacterRoot->GetAttachParent() == ExpectedAttachmentPoint;
		if (!bAttachedToExpectedPoint
			|| RelativeToPoint.GetLocation().SizeSquared() > FMath::Square(0.5)
			|| RotationErrorDegrees > 0.5f)
		{
			const UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement();
			const UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo();
			UE_LOG(LogOceanAdventure, Warning,
				TEXT("[NavalStationTrace] phase=control-sample-presentation result=drift avatar=%s station=%s expected_point=%s attach_parent=%s attached_to_expected=%d relative_location=%s relative_rotation=%s avatar_location=%s point_location=%s velocity=%s acceleration=%s pending_input=%s last_input=%s movement_mode=%d movement_stopped_count=%d local=%d authority=%d"),
				*GetNameSafe(Character), *GetNameSafe(Station), *GetNameSafe(ExpectedAttachmentPoint),
				*GetNameSafe(CharacterRoot->GetAttachParent()), bAttachedToExpectedPoint,
				*RelativeToPoint.GetLocation().ToCompactString(),
				*RelativeToPoint.Rotator().ToCompactString(),
				*Character->GetActorLocation().ToCompactString(),
				*ExpectedAttachmentPoint->GetComponentLocation().ToCompactString(),
				CharacterMovement ? *CharacterMovement->Velocity.ToCompactString() : TEXT("None"),
				CharacterMovement ? *CharacterMovement->GetCurrentAcceleration().ToCompactString() : TEXT("None"),
				*Character->GetPendingMovementInputVector().ToCompactString(),
				*Character->GetLastMovementInputVector().ToCompactString(),
				CharacterMovement ? static_cast<int32>(CharacterMovement->MovementMode) : -1,
				AbilitySystem ? AbilitySystem->GetTagCount(TAG_Gameplay_MovementStopped) : -1,
				CurrentActorInfo && CurrentActorInfo->IsLocallyControlled(),
				HasAuthority(&CurrentActivationInfo));
		}
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
	bHasSavedMovementMode = false;

	// The GAS lock blocks input and other abilities. CharacterMovement must also be suspended:
	// even at zero max speed its physics step can rewrite an attached root component's relative
	// transform through floor adjustment, gravity, or movement-base handling.
	if (!ApplyStationLock())
	{
		return;
	}

	UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement();
	if (CharacterMovement)
	{
		CharacterMovement->StopMovementImmediately();
	}
	Character->ConsumeMovementInputVector();

	USceneComponent* AttachmentPoint = FindOperatorAttachmentPoint(Station);
	bool bAttached = false;
	if (AttachmentPoint)
	{
		bAttached = Character->AttachToComponent(
			AttachmentPoint,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
	else
	{
		FTransform OperatorTransform;
		if (GetOperatorTransform(Station, OperatorTransform))
		{
			Character->SetActorLocationAndRotation(
				OperatorTransform.GetLocation(),
				OperatorTransform.Rotator(),
				/*bSweep=*/false,
				nullptr,
				ETeleportType::TeleportPhysics);
			bAttached = Character->AttachToActor(Station, FAttachmentTransformRules::KeepWorldTransform);
		}
	}

	if (!bAttached)
	{
		UE_LOG(LogOceanAdventure, Error,
			TEXT("[NavalStation] Presentation attachment failed avatar=%s station=%s attachment_point=%s"),
			*GetNameSafe(Character), *GetNameSafe(Station), *GetNameSafe(AttachmentPoint));
		RemoveStationLock();
		return;
	}

	if (CharacterMovement)
	{
		SavedMovementMode = CharacterMovement->MovementMode;
		SavedCustomMovementMode = CharacterMovement->CustomMovementMode;
		bHasSavedMovementMode = true;
		CharacterMovement->DisableMovement();
	}

	const FGameplayTag StatusTag = GetStationStatusTag();
	UE_LOG(LogOceanAdventure, Display,
		TEXT("[NavalStation] Presentation entered avatar=%s station=%s status=%s attachment_point=%s attached_to=%s relative_location=%s saved_movement_mode=%d saved_custom_mode=%d current_movement_mode=%d"),
		*GetNameSafe(Character), *GetNameSafe(Station), *StatusTag.ToString(),
		*GetNameSafe(AttachmentPoint), *GetNameSafe(Character->GetRootComponent()->GetAttachParent()),
		*Character->GetRootComponent()->GetRelativeLocation().ToCompactString(),
		bHasSavedMovementMode ? static_cast<int32>(SavedMovementMode.GetValue()) : -1,
		bHasSavedMovementMode ? static_cast<int32>(SavedCustomMovementMode) : -1,
		CharacterMovement ? static_cast<int32>(CharacterMovement->MovementMode) : -1);

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
		bHasSavedMovementMode = false;
		return;
	}

	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
	{
		if (bHasSavedMovementMode)
		{
			const EMovementMode MovementModeBeforeRestore = CharacterMovement->MovementMode;
			const EMovementMode RestoredMovementMode = SavedMovementMode == MOVE_None
				? MOVE_Falling
				: SavedMovementMode.GetValue();
			CharacterMovement->SetMovementMode(RestoredMovementMode, SavedCustomMovementMode);
			UE_LOG(LogOceanAdventure, Display,
				TEXT("[NavalStationTrace] phase=movement-mode-restore avatar=%s before=%d saved=%d saved_custom=%d restored=%d restored_custom=%d"),
				*GetNameSafe(Character), static_cast<int32>(MovementModeBeforeRestore),
				static_cast<int32>(SavedMovementMode.GetValue()),
				static_cast<int32>(SavedCustomMovementMode),
				static_cast<int32>(CharacterMovement->MovementMode),
				static_cast<int32>(CharacterMovement->CustomMovementMode));
		}
	}

	bHasSavedMovementMode = false;
}

bool UOceanAdventureGameplayAbility_NavalStation::ApplyStationLock()
{
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo();
	if (!AbilitySystem)
	{
		UE_LOG(LogOceanAdventure, Error,
			TEXT("[NavalInputTrace] phase=station-lock-apply result=no-asc ability=%s avatar=%s"),
			*GetNameSafe(this), *GetNameSafe(GetAvatarActorFromActorInfo()));
		return false;
	}

	FGameplayEffectSpecHandle SpecHandle =
		MakeOutgoingGameplayEffectSpec(
			CurrentSpecHandle,
			CurrentActorInfo,
			CurrentActivationInfo,
			UOceanAdventureGameplayEffect_NavalStationLock::StaticClass(),
			1.0f);
	FGameplayEffectSpec* Spec = SpecHandle.Data.Get();
	if (!Spec)
	{
		UE_LOG(LogOceanAdventure, Error,
			TEXT("[NavalInputTrace] phase=station-lock-apply result=no-spec ability=%s avatar=%s asc=%s"),
			*GetNameSafe(this), *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(AbilitySystem));
		return false;
	}

	Spec->DynamicGrantedTags.AddTag(TAG_Gameplay_MovementStopped);
	const FGameplayTag StatusTag = GetStationStatusTag();
	if (StatusTag.IsValid())
	{
		Spec->DynamicGrantedTags.AddTag(StatusTag);
	}

	// The ability wrapper supplies GetPredictionKeyForNewAction(). Calling the ASC overload
	// without its second argument would pass an invalid prediction key and reject this
	// non-instant effect on an autonomous proxy.
	StationLockHandle = ApplyGameplayEffectSpecToOwner(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		SpecHandle);
	if (!StationLockHandle.IsValid())
	{
		UE_LOG(LogOceanAdventure, Error,
			TEXT("[NavalInputTrace] phase=station-lock-apply result=invalid-handle ability=%s avatar=%s asc=%s movement_stopped_count=%d status=%s status_count=%d"),
			*GetNameSafe(this), *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(AbilitySystem),
			AbilitySystem->GetTagCount(TAG_Gameplay_MovementStopped), *StatusTag.ToString(),
			StatusTag.IsValid() ? AbilitySystem->GetTagCount(StatusTag) : 0);
		return false;
	}

	StationLockAbilitySystem = AbilitySystem;
	UE_LOG(LogOceanAdventure, Display,
		TEXT("[NavalInputTrace] phase=station-lock-apply result=success ability=%s avatar=%s asc=%s movement_stopped_count=%d status=%s status_count=%d local=%d authority=%d"),
		*GetNameSafe(this), *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(AbilitySystem),
		AbilitySystem->GetTagCount(TAG_Gameplay_MovementStopped), *StatusTag.ToString(),
		StatusTag.IsValid() ? AbilitySystem->GetTagCount(StatusTag) : 0,
		CurrentActorInfo && CurrentActorInfo->IsLocallyControlled(),
		HasAuthority(&CurrentActivationInfo));
	return true;
}

void UOceanAdventureGameplayAbility_NavalStation::RemoveStationLock()
{
	UAbilitySystemComponent* AbilitySystem = StationLockAbilitySystem.Get();
	const FGameplayTag StatusTag = GetStationStatusTag();
	const int32 MovementStoppedCountBefore = AbilitySystem
		? AbilitySystem->GetTagCount(TAG_Gameplay_MovementStopped)
		: -1;
	const int32 StatusCountBefore = AbilitySystem && StatusTag.IsValid()
		? AbilitySystem->GetTagCount(StatusTag)
		: -1;
	bool bRemoved = false;

	if (StationLockHandle.IsValid())
	{
		if (AbilitySystem)
		{
			bRemoved = AbilitySystem->RemoveActiveGameplayEffect(StationLockHandle);
		}
	}

	UE_LOG(LogOceanAdventure, Display,
		TEXT("[NavalInputTrace] phase=station-lock-remove ability=%s avatar=%s asc=%s handle_valid=%d removed=%d movement_stopped_before=%d movement_stopped_after=%d status=%s status_before=%d status_after=%d"),
		*GetNameSafe(this), *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(AbilitySystem),
		StationLockHandle.IsValid(), bRemoved, MovementStoppedCountBefore,
		AbilitySystem ? AbilitySystem->GetTagCount(TAG_Gameplay_MovementStopped) : -1,
		*StatusTag.ToString(), StatusCountBefore,
		AbilitySystem && StatusTag.IsValid() ? AbilitySystem->GetTagCount(StatusTag) : -1);

	StationLockHandle = FActiveGameplayEffectHandle();
	StationLockAbilitySystem.Reset();
}

void UOceanAdventureGameplayAbility_NavalStation::ApplyExitLock()
{
	if (StationExitLockSeconds <= 0.0f || !CurrentActorInfo)
	{
		return;
	}

	// Stepping off a gun and shooting has to cost something, or the gun would have no
	// downside at close range at all. A duration GE owns both replication and expiry, so
	// repeated exits cannot leak a loose-tag count or leave an uncancellable timer behind.
	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		UOceanAdventureGameplayEffect_NavalStationExitLock::StaticClass(),
		1.0f);
	FGameplayEffectSpec* Spec = SpecHandle.Data.Get();
	if (!Spec)
	{
		UE_LOG(LogOceanAdventure, Error,
			TEXT("[NavalStation] Failed to create station exit-lock effect ability=%s avatar=%s"),
			*GetNameSafe(this), *GetNameSafe(GetAvatarActorFromActorInfo()));
		return;
	}

	Spec->SetDuration(StationExitLockSeconds, /*bLockDuration=*/true);
	Spec->DynamicGrantedTags.AddTag(OceanAdventureNavalTags::Status_Naval_StationExitLock);

	const FActiveGameplayEffectHandle ExitLockHandle = ApplyGameplayEffectSpecToOwner(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		SpecHandle);
	if (!ExitLockHandle.IsValid())
	{
		UE_LOG(LogOceanAdventure, Warning,
			TEXT("[NavalStation] Failed to apply station exit-lock effect ability=%s avatar=%s duration=%.2f"),
			*GetNameSafe(this), *GetNameSafe(GetAvatarActorFromActorInfo()), StationExitLockSeconds);
	}
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

USceneComponent* UOceanAdventureGameplayAbility_NavalStation::FindOperatorAttachmentPoint(
	AActor* /*Station*/) const
{
	return nullptr;
}
