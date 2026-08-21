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
		return false;
	}

	// No host in reach means the key does nothing at all -- no ghost, no HUD, no cursor.
	const UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	return AbilitySystem
		&& AbilitySystem->HasMatchingGameplayTag(OceanAdventureBuildTags::Status_Build_HostAvailable);
}

void UOceanAdventureGameplayAbility_BuildMode::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Runs on both the owning client and the server (LocalPredicted), so the placement
	// abilities can require this tag on either side.
	if (UAbilitySystemComponent* AbilitySystem = ActorInfo->AbilitySystemComponent.Get())
	{
		AbilitySystem->AddLooseGameplayTag(OceanAdventureBuildTags::Status_Build_Active);
	}

	if (!ActorInfo->IsLocallyControlled())
	{
		return;
	}

	if (UBuildPreviewComponent* Preview = GetPreviewComponent())
	{
		Preview->SetSelectedPieceIndex(DefaultPieceIndex);
		Preview->SetPreviewEnabled(true);
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
			}
		}
	}
}

void UOceanAdventureGameplayAbility_BuildMode::EndAbility(
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
	CancelAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateCancelAbility=*/true);
}

UBuildPreviewComponent* UOceanAdventureGameplayAbility_BuildMode::GetPreviewComponent() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	return Avatar ? Avatar->FindComponentByClass<UBuildPreviewComponent>() : nullptr;
}
