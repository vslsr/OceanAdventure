// Copyright Epic Games, Inc. All Rights Reserved.

#include "Carry/OceanAdventureGameplayAbility_Carry.h"

#include "AbilitySystemComponent.h"
#include "Carry/CarrierComponent.h"
#include "Carry/CarryableComponent.h"
#include "Carry/CarryGameplayTags.h"
#include "Carry/OceanAdventureCarryMessages.h"
#include "Carry/OceanAdventureCarryTags.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalHeavyWeaponActor.h"
#include "Naval/NavalPartComponent.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureNavalTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_Carry)

UOceanAdventureGameplayAbility_Carry::UOceanAdventureGameplayAbility_Carry(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// Hands busy at a station are not free to lift anything. Leaving the wheel or the gun
	// first is the same commitment the design asks for everywhere else.
	ActivationBlockedTags.AddTag(OceanAdventureNavalTags::Status_Naval_Steering);
	ActivationBlockedTags.AddTag(OceanAdventureNavalTags::Status_Naval_OperatingHeavyWeapon);
}

UCarrierComponent* UOceanAdventureGameplayAbility_Carry::GetCarrierComponent() const
{
	return UCarrierComponent::FindCarrier(GetAvatarActorFromActorInfo());
}

bool UOceanAdventureGameplayAbility_Carry::CanCarryTarget(
	const UCarryableComponent* Carryable, FGameplayTag& OutFailReason) const
{
	const AActor* CarryActor = Carryable ? Carryable->GetOwner() : nullptr;
	if (!CarryActor)
	{
		OutFailReason = CarryGameplayTags::Fail_Carry_Invalid;
		return false;
	}

	const ANavalHeavyWeaponActor* Weapon = Cast<ANavalHeavyWeaponActor>(CarryActor);
	if (!Weapon)
	{
		// Nothing else carries naval rules yet; a crate or a cargo box passes on the
		// framework's checks alone.
		return true;
	}

	if (UNavalVesselComponent::FindVessel(Weapon) != nullptr)
	{
		OutFailReason = OceanAdventureCarryTags::Fail_Carry_Mounted;
		return false;
	}
	if (Weapon->GetWeaponOperator() != nullptr)
	{
		// Including the carrier themselves: step off the gun, then lift it.
		OutFailReason = CarryGameplayTags::Fail_Carry_Occupied;
		return false;
	}

	const UNavalPartComponent* Part = Weapon->GetPart();
	if (!Part || !Part->IsFunctional())
	{
		// Half-built and wrecked guns are targets, not luggage. Picking one up mid-ramp
		// would also hand the player a free way to cancel the deploy risk.
		OutFailReason = Part && Part->GetPartState() == ENavalPartState::UnderConstruction
			? NavalGameplayTags::Fail_UnderConstruction
			: NavalGameplayTags::Fail_NotOperational;
		return false;
	}

	return true;
}

void UOceanAdventureGameplayAbility_Carry::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bCarryResolved = false;
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

	UCarrierComponent* Carrier = GetCarrierComponent();
	if (!Carrier)
	{
		// No carrier component means this experience never injected one; nothing to say.
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FOceanAdventureCarryTargetData CarryData;
	FGameplayTag FailReason;

	if (Carrier->IsCarrying())
	{
		UCarryableComponent* Carried = Carrier->GetCarried();
		if (!Carrier->CanPutDown(FailReason))
		{
			BroadcastFailure(FailReason, Carried ? Carried->GetOwner() : nullptr);
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		CarryData.Request = EOceanAdventureCarryRequest::PutDown;
		CarryData.CarryTarget = Carried->GetOwner();
	}
	else
	{
		UCarryableComponent* Target = Carrier->FindBestCarryTarget();
		if (!Target)
		{
			BroadcastFailure(CarryGameplayTags::Fail_Carry_NoTarget, nullptr);
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		if (!Carrier->CanPickUp(Target, FailReason) || !CanCarryTarget(Target, FailReason))
		{
			BroadcastFailure(FailReason, Target->GetOwner());
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		CarryData.Request = EOceanAdventureCarryRequest::PickUp;
		CarryData.CarryTarget = Target->GetOwner();
	}

	FGameplayAbilityTargetDataHandle DataHandle;
	DataHandle.Add(new FOceanAdventureCarryTargetData(CarryData));
	OnTargetDataReadyCallback(DataHandle, FGameplayTag());
}

void UOceanAdventureGameplayAbility_Carry::OnTargetDataReadyCallback(
	const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag)
{
	UAbilitySystemComponent* AbilitySystem = CurrentActorInfo
		? CurrentActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	if (!AbilitySystem)
	{
		return;
	}

	if (bCarryResolved)
	{
		AbilitySystem->ConsumeClientReplicatedTargetData(
			CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
		return;
	}
	bCarryResolved = true;

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
		const FOceanAdventureCarryTargetData* Data =
			static_cast<const FOceanAdventureCarryTargetData*>(LocalData.Get(0));
		UCarrierComponent* Carrier = GetCarrierComponent();

		// Nothing here is predicted: the attachment and the collision state are replicated
		// truth, and the client only presents what the server writes.
		if (Data && Carrier)
		{
			if (Data->Request == EOceanAdventureCarryRequest::PutDown)
			{
				Carrier->ServerPutDown();
			}
			else
			{
				UCarryableComponent* Target = UCarryableComponent::FindCarryable(Data->CarryTarget.Get());
				FGameplayTag FailReason;

				// The client picked the target; the server decides whether it may have it.
				if (Target && Carrier->CanPickUp(Target, FailReason) && CanCarryTarget(Target, FailReason))
				{
					Carrier->ServerPickUp(Target);
				}
			}
		}
	}

	AbilitySystem->ConsumeClientReplicatedTargetData(
		CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UOceanAdventureGameplayAbility_Carry::EndAbility(
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
	}
	OnTargetDataReadyHandle.Reset();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UOceanAdventureGameplayAbility_Carry::BroadcastFailure(FGameplayTag FailReason, AActor* CarryTarget) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FOceanAdventureCarryFailedMessage Message;
	Message.FailReason = FailReason;
	Message.CarryTarget = CarryTarget;
	Message.Instigator = GetAvatarActorFromActorInfo();
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(
		OceanAdventureCarryTags::Message_Carry_Failed, Message);
}
