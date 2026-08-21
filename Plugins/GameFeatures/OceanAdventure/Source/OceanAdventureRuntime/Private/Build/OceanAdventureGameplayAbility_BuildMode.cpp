// Copyright Epic Games, Inc. All Rights Reserved.

#include "Build/OceanAdventureGameplayAbility_BuildMode.h"

#include "AbilitySystemComponent.h"
#include "Build/OceanAdventureBuildInputWidget.h"
#include "Build/OceanAdventureBuildTags.h"
#include "Building/BuildPreviewComponent.h"
#include "CommonActivatableWidget.h"
#include "CommonUIExtensions.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_BuildMode)

UOceanAdventureGameplayAbility_BuildMode::UOceanAdventureGameplayAbility_BuildMode(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	BuildInputWidgetClass = UOceanAdventureBuildInputWidget::StaticClass();
	UILayerTag = FGameplayTag::RequestGameplayTag(FName("UI.Layer.Game"), /*ErrorIfNotFound=*/false);
}

bool UOceanAdventureGameplayAbility_BuildMode::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[BuildPlacement] BuildMode CanActivate rejected stage=Super avatar=%s spec=%s local=%d authority=%d relevant_tags=%s"),
			*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
			*Handle.ToString(),
			ActorInfo && ActorInfo->IsLocallyControlled(),
			ActorInfo && ActorInfo->IsNetAuthority(),
			OptionalRelevantTags ? *OptionalRelevantTags->ToStringSimple() : TEXT("<none>"));
		return false;
	}

	// No host in reach means the key does nothing at all -- no ghost, no HUD, no cursor.
	const UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const bool bHostAvailable = AbilitySystem
		&& AbilitySystem->HasMatchingGameplayTag(OceanAdventureBuildTags::Status_Build_HostAvailable);
	if (!bHostAvailable)
	{
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[BuildPlacement] BuildMode CanActivate rejected stage=HostAvailableTag avatar=%s spec=%s asc=%s host_tag=%d local=%d authority=%d"),
			*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
			*Handle.ToString(),
			*GetNameSafe(AbilitySystem),
			AbilitySystem && AbilitySystem->HasMatchingGameplayTag(OceanAdventureBuildTags::Status_Build_HostAvailable),
			ActorInfo && ActorInfo->IsLocallyControlled(),
			ActorInfo && ActorInfo->IsNetAuthority());
	}
	return bHostAvailable;
}

void UOceanAdventureGameplayAbility_BuildMode::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	UE_LOG(
		LogOceanAdventure,
		Display,
		TEXT("[BuildPlacement] BuildMode Activate avatar=%s spec=%s prediction=%s local=%d authority=%d"),
		*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
		*Handle.ToString(),
		*ActivationInfo.GetActivationPredictionKey().ToString(),
		ActorInfo && ActorInfo->IsLocallyControlled(),
		ActorInfo && ActorInfo->IsNetAuthority());

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[BuildPlacement] BuildMode Activate rejected stage=Commit avatar=%s spec=%s prediction=%s"),
			*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
			*Handle.ToString(),
			*ActivationInfo.GetActivationPredictionKey().ToString());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Runs on both the owning client and the server (LocalPredicted), so the placement
	// abilities can require this tag on either side.
	if (UAbilitySystemComponent* AbilitySystem = ActorInfo->AbilitySystemComponent.Get())
	{
		AbilitySystem->AddLooseGameplayTag(OceanAdventureBuildTags::Status_Build_Active);
		UE_LOG(
			LogOceanAdventure,
			Display,
			TEXT("[BuildPlacement] BuildMode active tag added avatar=%s local=%d authority=%d tag_count=%d"),
			*GetNameSafe(ActorInfo->AvatarActor.Get()),
			ActorInfo->IsLocallyControlled(),
			ActorInfo->IsNetAuthority(),
			AbilitySystem->GetTagCount(OceanAdventureBuildTags::Status_Build_Active));
	}

	if (!ActorInfo->IsLocallyControlled())
	{
		UE_LOG(
			LogOceanAdventure,
			Display,
			TEXT("[BuildPlacement] BuildMode server activation complete avatar=%s"),
			*GetNameSafe(ActorInfo->AvatarActor.Get()));
		return;
	}

	if (UBuildPreviewComponent* Preview = GetPreviewComponent())
	{
		Preview->SetSelectedPieceIndex(DefaultPieceIndex);
		Preview->SetPreviewEnabled(true);
		UE_LOG(
			LogOceanAdventure,
			Display,
			TEXT("[BuildPlacement] BuildMode preview enabled avatar=%s preview=%s piece=%d"),
			*GetNameSafe(ActorInfo->AvatarActor.Get()),
			*GetNameSafe(Preview),
			DefaultPieceIndex);
	}
	else
	{
		UE_LOG(
			LogOceanAdventure,
			Error,
			TEXT("[BuildPlacement] BuildMode missing preview component avatar=%s"),
			*GetNameSafe(ActorInfo->AvatarActor.Get()));
	}

	// Cursor and input routing come from the widget's FUIInputConfig, never from
	// APlayerController::bShowMouseCursor.
	if (BuildInputWidgetClass && UILayerTag.IsValid())
	{
		if (const APlayerController* PlayerController = Cast<APlayerController>(ActorInfo->PlayerController.Get()))
		{
			if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
			{
				PushedInputWidget = UCommonUIExtensions::PushContentToLayer_ForPlayer(
					LocalPlayer,
					UILayerTag,
					BuildInputWidgetClass);
				UE_LOG(
					LogOceanAdventure,
					Display,
					TEXT("[BuildPlacement] BuildMode input widget push widget=%s success=%d widget_class=%s layer=%s player=%s"),
					*GetNameSafe(PushedInputWidget),
					PushedInputWidget != nullptr,
					*GetNameSafe(BuildInputWidgetClass),
					*UILayerTag.ToString(),
					*GetNameSafe(LocalPlayer));
			}
		}
	}
	else
	{
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[BuildPlacement] BuildMode input widget skipped widget_class=%s layer=%s"),
			*GetNameSafe(BuildInputWidgetClass),
			*UILayerTag.ToString());
	}
}

void UOceanAdventureGameplayAbility_BuildMode::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	UE_LOG(
		LogOceanAdventure,
		Display,
		TEXT("[BuildPlacement] BuildMode End avatar=%s spec=%s prediction=%s cancelled=%d local=%d authority=%d"),
		*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
		*Handle.ToString(),
		*ActivationInfo.GetActivationPredictionKey().ToString(),
		bWasCancelled,
		ActorInfo && ActorInfo->IsLocallyControlled(),
		ActorInfo && ActorInfo->IsNetAuthority());

	if (ActorInfo)
	{
		if (UAbilitySystemComponent* AbilitySystem = ActorInfo->AbilitySystemComponent.Get())
		{
			AbilitySystem->RemoveLooseGameplayTag(OceanAdventureBuildTags::Status_Build_Active);
		}
	}

	if (UBuildPreviewComponent* Preview = GetPreviewComponent())
	{
		Preview->SetPreviewEnabled(false);
	}

	if (PushedInputWidget)
	{
		// Popping restores the layer below's input config; nothing to save or restore by hand.
		UCommonUIExtensions::PopContentFromLayer(PushedInputWidget);
		PushedInputWidget = nullptr;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOceanAdventureGameplayAbility_BuildMode::InputPressed(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputPressed(Handle, ActorInfo, ActivationInfo);
	UE_LOG(
		LogOceanAdventure,
		Display,
		TEXT("[BuildPlacement] BuildMode input pressed again; cancelling avatar=%s spec=%s prediction=%s"),
		*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
		*Handle.ToString(),
		*ActivationInfo.GetActivationPredictionKey().ToString());
	CancelAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateCancelAbility=*/true);
}

UBuildPreviewComponent* UOceanAdventureGameplayAbility_BuildMode::GetPreviewComponent() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	return Avatar ? Avatar->FindComponentByClass<UBuildPreviewComponent>() : nullptr;
}
