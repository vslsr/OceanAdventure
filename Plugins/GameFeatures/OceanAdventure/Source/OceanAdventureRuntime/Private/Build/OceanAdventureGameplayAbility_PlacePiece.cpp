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
#include "OceanAdventureRuntimeModule.h"

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
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[BuildPlacement] CanActivate rejected stage=Super avatar=%s spec=%s local=%d authority=%d relevant_tags=%s"),
			*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
			*Handle.ToString(),
			ActorInfo && ActorInfo->IsLocallyControlled(),
			ActorInfo && ActorInfo->IsNetAuthority(),
			OptionalRelevantTags ? *OptionalRelevantTags->ToStringSimple() : TEXT("<none>"));
		return false;
	}

	// Placement only exists inside build mode, on both client and server.
	const UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const bool bBuildModeActive = AbilitySystem
		&& AbilitySystem->HasMatchingGameplayTag(OceanAdventureBuildTags::Status_Build_Active);
	if (!bBuildModeActive)
	{
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[BuildPlacement] CanActivate rejected stage=BuildModeTag avatar=%s spec=%s asc=%s active_tag=%d local=%d authority=%d"),
			*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
			*Handle.ToString(),
			*GetNameSafe(AbilitySystem),
			AbilitySystem && AbilitySystem->HasMatchingGameplayTag(OceanAdventureBuildTags::Status_Build_Active),
			ActorInfo && ActorInfo->IsLocallyControlled(),
			ActorInfo && ActorInfo->IsNetAuthority());
	}
	return bBuildModeActive;
}

void UOceanAdventureGameplayAbility_PlacePiece::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	UE_LOG(
		LogOceanAdventure,
		Display,
		TEXT("[BuildPlacement] Activate avatar=%s spec=%s prediction=%s local=%d authority=%d"),
		*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
		*Handle.ToString(),
		*ActivationInfo.GetActivationPredictionKey().ToString(),
		ActorInfo && ActorInfo->IsLocallyControlled(),
		ActorInfo && ActorInfo->IsNetAuthority());

	UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystem)
	{
		UE_LOG(
			LogOceanAdventure,
			Error,
			TEXT("[BuildPlacement] Activate aborted stage=MissingASC avatar=%s spec=%s"),
			*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
			*Handle.ToString());
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
		UE_LOG(
			LogOceanAdventure,
			Display,
			TEXT("[BuildPlacement] Server waiting for TargetData avatar=%s spec=%s prediction=%s"),
			*GetNameSafe(ActorInfo->AvatarActor.Get()),
			*Handle.ToString(),
			*ActivationInfo.GetActivationPredictionKey().ToString());
		return;
	}

	FOceanAdventureBuildTargetData LocalData;
	FGameplayTag FailReason;
	if (!BuildLocalTargetData(LocalData, FailReason))
	{
		const UBuildPreviewComponent* Preview = GetPreviewComponent();
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[BuildPlacement] Local target rejected avatar=%s host=%s key=%s slot=%d edge=%u sub_cell=%u piece=%d preview_enabled=%d preview_valid=%d reason=%s"),
			*GetNameSafe(ActorInfo->AvatarActor.Get()),
			*GetNameSafe(LocalData.HostActor.Get()),
			*LocalData.SlotKey.Coord.ToString(),
			static_cast<int32>(LocalData.SlotKey.Slot),
			LocalData.SlotKey.EdgeIndex,
			LocalData.SlotKey.SubCell,
			Preview ? Preview->GetSelectedPieceIndex() : INDEX_NONE,
			Preview && Preview->IsPreviewEnabled(),
			Preview && Preview->IsPlacementValid(),
			*FailReason.ToString());
		// Rejected before anything leaves the machine: the ghost already showed why.
		if (UBuildPreviewComponent* MutablePreview = GetPreviewComponent())
		{
			MutablePreview->TriggerFailureFeedback(FailReason);
		}
		BroadcastFailure(FailReason, LocalData.HostActor.Get());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UE_LOG(
		LogOceanAdventure,
		Display,
		TEXT("[BuildPlacement] Local target accepted avatar=%s host=%s key=%s slot=%d edge=%u sub_cell=%u piece=%u rotation=%u prediction=%s"),
		*GetNameSafe(ActorInfo->AvatarActor.Get()),
		*GetNameSafe(LocalData.HostActor.Get()),
		*LocalData.SlotKey.Coord.ToString(),
		static_cast<int32>(LocalData.SlotKey.Slot),
		LocalData.SlotKey.EdgeIndex,
		LocalData.SlotKey.SubCell,
		LocalData.PieceIndex,
		LocalData.Rotation,
		*ActivationInfo.GetActivationPredictionKey().ToString());

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
		UE_LOG(LogOceanAdventure, Error, TEXT("[BuildPlacement] TargetData callback aborted stage=MissingASC"));
		return;
	}

	UE_LOG(
		LogOceanAdventure,
		Display,
		TEXT("[BuildPlacement] TargetData callback avatar=%s items=%d prediction=%s local=%d authority=%d"),
		*GetNameSafe(CurrentActorInfo ? CurrentActorInfo->AvatarActor.Get() : nullptr),
		InData.Num(),
		*CurrentActivationInfo.GetActivationPredictionKey().ToString(),
		CurrentActorInfo && CurrentActorInfo->IsLocallyControlled(),
		CurrentActorInfo && CurrentActorInfo->IsNetAuthority());

	FScopedPredictionWindow ScopedPrediction(
		AbilitySystem,
		CurrentActivationInfo.GetActivationPredictionKey());

	FGameplayAbilityTargetDataHandle LocalData(
		MoveTemp(const_cast<FGameplayAbilityTargetDataHandle&>(InData)));

	if (CurrentActorInfo->IsLocallyControlled() && !CurrentActorInfo->IsNetAuthority())
	{
		UE_LOG(
			LogOceanAdventure,
			Display,
			TEXT("[BuildPlacement] Sending TargetData to server spec=%s prediction=%s items=%d"),
			*CurrentSpecHandle.ToString(),
			*CurrentActivationInfo.GetActivationPredictionKey().ToString(),
			LocalData.Num());
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
		const bool bApplied = Structure && Data && ApplyToStructure(Structure, *Data, FailReason);
		if (!bApplied)
		{
			UE_LOG(
				LogOceanAdventure,
				Warning,
				TEXT("[BuildPlacement] Server rejected host=%s structure=%s key=%s slot=%d edge=%u sub_cell=%u piece=%u rotation=%u reason=%s prediction=%s"),
				*GetNameSafe(HostActor),
				*GetNameSafe(Structure),
				Data ? *Data->SlotKey.Coord.ToString() : TEXT("<no-data>"),
				Data ? static_cast<int32>(Data->SlotKey.Slot) : INDEX_NONE,
				Data ? Data->SlotKey.EdgeIndex : 0,
				Data ? Data->SlotKey.SubCell : FBuildSlotKey::CenterSubCell,
				Data ? Data->PieceIndex : 0,
				Data ? Data->Rotation : 0,
				*FailReason.ToString(),
				*CurrentActivationInfo.GetActivationPredictionKey().ToString());
			// The server owns the verdict; the client already had its local preview.
			BroadcastFailure(
				FailReason.IsValid() ? FailReason : BuildGameplayTags::Fail_BadDefinition,
				HostActor);
		}
		else
		{
			UE_LOG(
				LogOceanAdventure,
				Display,
				TEXT("[BuildPlacement] Server accepted host=%s key=%s slot=%d edge=%u sub_cell=%u piece=%u rotation=%u prediction=%s"),
				*GetNameSafe(HostActor),
				*Data->SlotKey.Coord.ToString(),
				static_cast<int32>(Data->SlotKey.Slot),
				Data->SlotKey.EdgeIndex,
				Data->SlotKey.SubCell,
				Data->PieceIndex,
				Data->Rotation,
				*CurrentActivationInfo.GetActivationPredictionKey().ToString());
		}
	}
	else if (HasAuthority(&CurrentActivationInfo))
	{
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[BuildPlacement] Server callback had no TargetData spec=%s prediction=%s"),
			*CurrentSpecHandle.ToString(),
			*CurrentActivationInfo.GetActivationPredictionKey().ToString());
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
