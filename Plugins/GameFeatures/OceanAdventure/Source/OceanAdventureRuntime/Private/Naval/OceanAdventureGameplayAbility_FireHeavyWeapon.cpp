// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_FireHeavyWeapon.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalHeavyWeaponActor.h"
#include "Naval/OceanAdventureNavalMessages.h"
#include "Naval/OceanAdventureNavalStatics.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_FireHeavyWeapon)

UOceanAdventureGameplayAbility_FireHeavyWeapon::UOceanAdventureGameplayAbility_FireHeavyWeapon(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

bool UOceanAdventureGameplayAbility_FireHeavyWeapon::CanActivateAbility(
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

	// Firing only exists while the player is actually at a gun, on both client and server.
	const UAbilitySystemComponent* AbilitySystem = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	return AbilitySystem
		&& AbilitySystem->HasMatchingGameplayTag(OceanAdventureNavalTags::Status_Naval_OperatingHeavyWeapon);
}

ANavalHeavyWeaponActor* UOceanAdventureGameplayAbility_FireHeavyWeapon::FindOperatedWeapon() const
{
	// The operate ability attached the character to the gun, so the attachment is the truth
	// about which gun this is -- no second search, and no way to fire a gun across the island.
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	return Avatar ? Cast<ANavalHeavyWeaponActor>(Avatar->GetAttachParentActor()) : nullptr;
}

void UOceanAdventureGameplayAbility_FireHeavyWeapon::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bFireResolved = false;
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

	ANavalHeavyWeaponActor* Weapon = FindOperatedWeapon();
	if (!Weapon)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetControllerFromActorInfo());
	FVector AimLocation = FVector::ZeroVector;
	if (!UOceanAdventureNavalStatics::GetCursorAimLocation(PlayerController, AimLocation))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// The same predicate the server will run. Failing here means the player is told why
	// without the round trip; passing here still proves nothing to the server.
	FGameplayTag FailReason;
	if (!Weapon->CanFire(GetAvatarActorFromActorInfo(), AimLocation, FailReason))
	{
		BroadcastFailure(FailReason, Weapon);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FOceanAdventureNavalTargetData FireData;
	FireData.StationActor = Weapon;
	FireData.Request = ENavalStationRequest::Fire;
	FireData.AimLocation = AimLocation;

	FGameplayAbilityTargetDataHandle DataHandle;
	DataHandle.Add(new FOceanAdventureNavalTargetData(FireData));
	OnTargetDataReadyCallback(DataHandle, FGameplayTag());
}

void UOceanAdventureGameplayAbility_FireHeavyWeapon::OnTargetDataReadyCallback(
	const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag)
{
	UAbilitySystemComponent* AbilitySystem = CurrentActorInfo
		? CurrentActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	if (!AbilitySystem)
	{
		return;
	}

	if (bFireResolved)
	{
		AbilitySystem->ConsumeClientReplicatedTargetData(
			CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
		return;
	}
	bFireResolved = true;

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
		ANavalHeavyWeaponActor* Weapon = Data
			? Cast<ANavalHeavyWeaponActor>(Data->StationActor.Get())
			: nullptr;

		// TryFire re-runs every check itself; the client's verdict is only a hint.
		if (!Weapon || !Weapon->TryFire(GetAvatarActorFromActorInfo(), Data->AimLocation))
		{
			UE_LOG(
				LogOceanAdventure,
				Verbose,
				TEXT("[NavalFire] Server refused weapon=%s avatar=%s"),
				*GetNameSafe(Weapon),
				*GetNameSafe(GetAvatarActorFromActorInfo()));
		}
	}

	AbilitySystem->ConsumeClientReplicatedTargetData(
		CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UOceanAdventureGameplayAbility_FireHeavyWeapon::EndAbility(
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

void UOceanAdventureGameplayAbility_FireHeavyWeapon::BroadcastFailure(
	FGameplayTag FailReason, AActor* Station) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FOceanAdventureNavalStationFailedMessage Message;
	Message.FailReason = FailReason.IsValid() ? FailReason : NavalGameplayTags::Fail_NotOperational;
	Message.StationActor = Station;
	Message.Instigator = GetAvatarActorFromActorInfo();
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(
		OceanAdventureNavalTags::Message_Naval_StationFailed, Message);
}
