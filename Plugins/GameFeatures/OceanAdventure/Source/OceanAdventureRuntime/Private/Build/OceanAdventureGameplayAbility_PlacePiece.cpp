// Copyright Epic Games, Inc. All Rights Reserved.

#include "Build/OceanAdventureGameplayAbility_PlacePiece.h"

#include "AbilitySystemComponent.h"
#include "Build/OceanAdventureBuildMessages.h"
#include "Build/OceanAdventureBuildTags.h"
#include "Building/BuildGameplayTags.h"
#include "Building/BuildPieceCatalog.h"
#include "Building/BuildPieceDefinition.h"
#include "Building/BuildPreviewComponent.h"
#include "Building/BuildStructureComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_PlacePiece)

UOceanAdventureGameplayAbility_PlacePiece::UOceanAdventureGameplayAbility_PlacePiece(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

bool UOceanAdventureGameplayAbility_PlacePiece::CanActivateAbility(
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

	// Placement only exists inside build mode, on both client and server.
	const UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	return AbilitySystem
		&& AbilitySystem->HasMatchingGameplayTag(OceanAdventureBuildTags::Status_Build_Active);
}

void UOceanAdventureGameplayAbility_PlacePiece::ActivateAbility(
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

	// The server waits on this delegate for the client's replicated target data; the local
	// path fires the same callback directly.
	OnTargetDataReadyHandle = AbilitySystem
		->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey())
		.AddUObject(this, &ThisClass::OnTargetDataReadyCallback);

	if (!ActorInfo->IsLocallyControlled())
	{
		return;
	}

	FOceanAdventureBuildTargetData LocalData;
	FGameplayTag FailReason;
	if (!BuildLocalTargetData(LocalData, FailReason))
	{
		// Rejected before anything leaves the machine: the ghost already showed why.
		if (UBuildPreviewComponent* Preview = GetPreviewComponent())
		{
			Preview->TriggerFailureFeedback(FailReason);
		}
		BroadcastFailure(FailReason, LocalData.HostActor.Get());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FGameplayAbilityTargetDataHandle DataHandle;
	DataHandle.Add(new FOceanAdventureBuildTargetData(LocalData));
	OnTargetDataReadyCallback(DataHandle, FGameplayTag());
}

void UOceanAdventureGameplayAbility_PlacePiece::OnTargetDataReadyCallback(
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
		AbilitySystem,
		CurrentActivationInfo.GetActivationPredictionKey());

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
		const FOceanAdventureBuildTargetData* Data =
			static_cast<const FOceanAdventureBuildTargetData*>(LocalData.Get(0));
		AActor* HostActor = Data ? Data->HostActor.Get() : nullptr;
		UBuildStructureComponent* Structure = HostActor
			? HostActor->FindComponentByClass<UBuildStructureComponent>()
			: nullptr;

		FGameplayTag FailReason;
		if (!Structure || !ApplyToStructure(Structure, *Data, FailReason))
		{
			// The server owns the verdict; the client already had its local preview.
			BroadcastFailure(
				FailReason.IsValid() ? FailReason : BuildGameplayTags::Fail_BadDefinition,
				HostActor);
		}
	}

	AbilitySystem->ConsumeClientReplicatedTargetData(
		CurrentSpecHandle,
		CurrentActivationInfo.GetActivationPredictionKey());

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UOceanAdventureGameplayAbility_PlacePiece::EndAbility(
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

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UOceanAdventureGameplayAbility_PlacePiece::BuildLocalTargetData(
	FOceanAdventureBuildTargetData& OutData,
	FGameplayTag& OutFailReason) const
{
	const UBuildPreviewComponent* Preview = GetPreviewComponent();
	UBuildStructureComponent* Structure = Preview ? Preview->GetCurrentStructure() : nullptr;
	if (!Structure)
	{
		OutFailReason = BuildGameplayTags::Fail_BadDefinition;
		return false;
	}

	OutData.HostActor = Structure->GetOwner();
	OutData.SlotKey = Preview->GetCurrentSlot();
	OutData.PieceIndex = static_cast<uint16>(FMath::Max(0, Preview->GetSelectedPieceIndex()));
	OutData.Rotation = 0;

	if (!Preview->IsPlacementValid())
	{
		OutFailReason = Preview->GetCurrentFailReason();
		return false;
	}
	return true;
}

bool UOceanAdventureGameplayAbility_PlacePiece::ApplyToStructure(
	UBuildStructureComponent* Structure,
	const FOceanAdventureBuildTargetData& Data,
	FGameplayTag& OutFailReason)
{
	const UBuildPieceCatalog* Catalog = Structure->GetCatalog();
	const UBuildPieceDefinition* Definition = Catalog ? Catalog->GetByIndex(Data.PieceIndex) : nullptr;
	if (!Definition)
	{
		OutFailReason = BuildGameplayTags::Fail_BadDefinition;
		return false;
	}

	return Structure->TryPlacePieceWithReason(
		Data.SlotKey,
		Definition,
		Data.Rotation,
		GetControllerFromActorInfo(),
		OutFailReason);
}

UBuildPreviewComponent* UOceanAdventureGameplayAbility_PlacePiece::GetPreviewComponent() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	return Avatar ? Avatar->FindComponentByClass<UBuildPreviewComponent>() : nullptr;
}

void UOceanAdventureGameplayAbility_PlacePiece::BroadcastFailure(
	FGameplayTag FailReason,
	AActor* HostActor) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FOceanAdventureBuildFailedMessage Message;
	Message.FailReason = FailReason;
	Message.HostActor = HostActor;
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(
		OceanAdventureBuildTags::Message_Build_Failed,
		Message);
}
