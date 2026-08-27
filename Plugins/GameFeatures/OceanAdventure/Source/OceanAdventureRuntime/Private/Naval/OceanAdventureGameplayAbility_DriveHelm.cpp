// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_DriveHelm.h"

#include "AbilitySystemComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Input/LyraInputConfig.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Naval/NavalHelmComponent.h"
#include "Naval/NavalHelmStation.h"
#include "Naval/OceanAdventureAbilityTask_NavalControl.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"
#include "UObject/SoftObjectPath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_DriveHelm)

namespace
{
	UNavalHelmComponent* ResolveHelm(AActor* Station)
	{
		INavalHelmStation* HelmStation = Station ? Cast<INavalHelmStation>(Station) : nullptr;
		return HelmStation ? HelmStation->GetHelmComponent() : nullptr;
	}
}

UOceanAdventureGameplayAbility_DriveHelm::UOceanAdventureGameplayAbility_DriveHelm(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DriveInputConfig(FSoftObjectPath(TEXT("/OceanAdventure/Input/DA_InputConfig_OceanNaval")))
	, HelmMappingContext(FSoftObjectPath(TEXT("/OceanAdventure/Input/IMC_OceanHelm")))
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnSpawn;
}

bool UOceanAdventureGameplayAbility_DriveHelm::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(
		Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	AActor* Station = FindSourceStation(Handle, ActorInfo);
	UNavalHelmComponent* Helm = ResolveHelm(Station);
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!Station || !Helm || !Avatar)
	{
		return false;
	}

	// The server must see the exact occupied station/operator. The owning client trusts the
	// temporary replicated spec because it may arrive before the Helm's replicated pointers.
	if (ActorInfo->IsNetAuthority())
	{
		return Helm->GetOperator() == Avatar && Helm->GetActiveStation() == Station;
	}
	// The replicated temporary spec was created only after server occupancy succeeded. Do not
	// make its one-shot OnSpawn activation depend on Helm replicated pointers arriving first.
	return true;
}

void UOceanAdventureGameplayAbility_DriveHelm::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UAbilitySystemComponent* AbilitySystem = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	SourceStation = FindSourceStation(Handle, ActorInfo);
	if (!AbilitySystem || !SourceStation.IsValid())
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

	if (!StartLocalInput())
	{
		UE_LOG(LogOceanAdventure, Error,
			TEXT("[DriveHelm] Tagged input assets are unavailable; ending drive ability avatar=%s"),
			*GetNameSafe(GetAvatarActorFromActorInfo()));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ControlTask = UOceanAdventureAbilityTask_NavalControl::NavalControlTick(
		this, ControlSampleInterval);
	if (ControlTask)
	{
		ControlTask->OnControlSample.AddUObject(this, &ThisClass::SampleAndSendControl);
		ControlTask->ReadyForActivation();
	}
}

void UOceanAdventureGameplayAbility_DriveHelm::EndAbility(
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
			AbilitySystem
				->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
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
	StopLocalInput();

	if (HasAuthority(&ActivationInfo))
	{
		if (UNavalHelmComponent* Helm = ResolveHelm(SourceStation.Get()))
		{
			Helm->SetControlIntent(GetAvatarActorFromActorInfo(), 0.0f, 0.0f);
		}
	}
	SourceStation.Reset();

	Super::EndAbility(
		Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOceanAdventureGameplayAbility_DriveHelm::OnRemoveAbility(
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilitySpec& Spec)
{
	// A server ClearAbility can remove the replicated temporary spec without a local input
	// edge. Always pop the context from the removal hook as a final leak guard.
	StopLocalInput();
	Super::OnRemoveAbility(ActorInfo, Spec);
}

AActor* UOceanAdventureGameplayAbility_DriveHelm::FindSourceStation(
	FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	UAbilitySystemComponent* AbilitySystem = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	const FGameplayAbilitySpec* Spec = AbilitySystem
		? AbilitySystem->FindAbilitySpecFromHandle(Handle)
		: nullptr;
	return Spec ? Cast<AActor>(Spec->SourceObject.Get()) : nullptr;
}

bool UOceanAdventureGameplayAbility_DriveHelm::StartLocalInput()
{
	const ULyraInputConfig* InputConfig = DriveInputConfig.LoadSynchronous();
	const UInputMappingContext* MappingContext = HelmMappingContext.LoadSynchronous();
	if (InputConfig)
	{
		for (const FLyraInputAction& Entry : InputConfig->AbilityInputActions)
		{
			if (Entry.InputTag == OceanAdventureNavalTags::InputTag_Naval_Helm_Throttle)
			{
				ThrottleAction = Entry.InputAction;
			}
			else if (Entry.InputTag == OceanAdventureNavalTags::InputTag_Naval_Helm_Steer)
			{
				SteerAction = Entry.InputAction;
			}
		}
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetControllerFromActorInfo());
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()
		: nullptr;
	if (!InputSubsystem || !MappingContext || !ThrottleAction || !SteerAction)
	{
		return false;
	}

	FModifyContextOptions Options = {};
	Options.bIgnoreAllPressedKeysUntilRelease = false;
	InputSubsystem->AddMappingContext(MappingContext, HelmMappingPriority, Options);
	bMappingPushed = true;
	return true;
}

void UOceanAdventureGameplayAbility_DriveHelm::StopLocalInput()
{
	if (bMappingPushed)
	{
		APlayerController* PlayerController = Cast<APlayerController>(GetControllerFromActorInfo());
		ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
			? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()
			: nullptr)
		{
			if (const UInputMappingContext* MappingContext = HelmMappingContext.Get())
			{
				InputSubsystem->RemoveMappingContext(MappingContext);
			}
		}
	}
	bMappingPushed = false;
	ThrottleAction = nullptr;
	SteerAction = nullptr;
}

void UOceanAdventureGameplayAbility_DriveHelm::SampleAndSendControl(float /*DeltaTime*/)
{
	APlayerController* PlayerController = Cast<APlayerController>(GetControllerFromActorInfo());
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()
		: nullptr;
	const UEnhancedPlayerInput* PlayerInput = InputSubsystem
		? InputSubsystem->GetPlayerInput()
		: nullptr;
	AActor* Station = SourceStation.Get();
	if (!PlayerInput || !Station || !ResolveHelm(Station))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	FOceanAdventureNavalTargetData Data;
	Data.StationActor = Station;
	Data.Request = ENavalStationRequest::Control;
	Data.SetControlIntent(
		PlayerInput->GetActionValue(ThrottleAction).Get<float>(),
		PlayerInput->GetActionValue(SteerAction).Get<float>());
	SendControlRequest(Data);
}

void UOceanAdventureGameplayAbility_DriveHelm::SendControlRequest(
	const FOceanAdventureNavalTargetData& Data)
{
	FGameplayAbilityTargetDataHandle DataHandle;
	DataHandle.Add(new FOceanAdventureNavalTargetData(Data));
	OnTargetDataReadyCallback(DataHandle, FGameplayTag());
}

void UOceanAdventureGameplayAbility_DriveHelm::OnTargetDataReadyCallback(
	const FGameplayAbilityTargetDataHandle& InData,
	FGameplayTag ApplicationTag)
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

	if (HasAuthority(&CurrentActivationInfo))
	{
		AActor* ExpectedStation = SourceStation.Get();
		UNavalHelmComponent* Helm = ResolveHelm(ExpectedStation);
		AActor* Avatar = GetAvatarActorFromActorInfo();
		for (int32 Index = 0; Index < LocalData.Num(); ++Index)
		{
			const FGameplayAbilityTargetData* RawData = LocalData.Get(Index);
			const FOceanAdventureNavalTargetData* Data = RawData
				&& RawData->GetScriptStruct() == FOceanAdventureNavalTargetData::StaticStruct()
				? static_cast<const FOceanAdventureNavalTargetData*>(RawData)
				: nullptr;
			if (Data
				&& Data->Request == ENavalStationRequest::Control
				&& Data->StationActor.Get() == ExpectedStation
				&& Helm
				&& Helm->GetOperator() == Avatar
				&& Helm->GetActiveStation() == ExpectedStation)
			{
				Helm->SetControlIntent(Avatar, Data->GetThrottle(), Data->GetSteer());
			}
		}
	}

	AbilitySystem->ConsumeClientReplicatedTargetData(
		CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
}
