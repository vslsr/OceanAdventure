// Copyright Epic Games, Inc. All Rights Reserved.

#include "TopDownGameplayAbility_Move.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Character/LyraCharacterMovementComponent.h"
#include "TopDownFeatureGameplayTags.h"
#include "TopDownPawnComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TopDownGameplayAbility_Move)

UTopDownGameplayAbility_Move::UTopDownGameplayAbility_Move(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::WhileInputActive;
	ActivationBlockedTags.AddTag(TAG_Gameplay_MovementStopped);
}

void UTopDownGameplayAbility_Move::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ActiveDirectionTag = ResolveDirectionTag(Handle);
	UAbilitySystemComponent* AbilitySystem = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	if (!AbilitySystem || !ActiveDirectionTag.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	MovementStoppedDelegateHandle = AbilitySystem
		->RegisterGameplayTagEvent(TAG_Gameplay_MovementStopped, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::OnMovementStoppedTagChanged);

	if (ActorInfo->IsLocallyControlled())
	{
		AActor* Avatar = ActorInfo->AvatarActor.Get();
		UTopDownPawnComponent* TopDownComponent = Avatar
			? Avatar->FindComponentByClass<UTopDownPawnComponent>()
			: nullptr;
		if (!TopDownComponent)
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		TopDownComponent->SetAbilityMoveInput(ActiveDirectionTag, true);
	}

	UAbilityTask_WaitInputRelease* ReleaseTask =
		UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	if (ReleaseTask)
	{
		ReleaseTask->OnRelease.AddDynamic(this, &ThisClass::OnInputReleased);
		ReleaseTask->ReadyForActivation();
	}
}

void UTopDownGameplayAbility_Move::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (ActorInfo && ActorInfo->IsLocallyControlled() && ActiveDirectionTag.IsValid())
	{
		if (AActor* Avatar = ActorInfo->AvatarActor.Get())
		{
			if (UTopDownPawnComponent* Component =
				Avatar->FindComponentByClass<UTopDownPawnComponent>())
			{
				Component->SetAbilityMoveInput(ActiveDirectionTag, false);
			}
		}
	}

	if (MovementStoppedDelegateHandle.IsValid() && ActorInfo)
	{
		if (UAbilitySystemComponent* AbilitySystem = ActorInfo->AbilitySystemComponent.Get())
		{
			AbilitySystem
				->RegisterGameplayTagEvent(TAG_Gameplay_MovementStopped, EGameplayTagEventType::NewOrRemoved)
				.Remove(MovementStoppedDelegateHandle);
		}
		MovementStoppedDelegateHandle.Reset();
	}
	ActiveDirectionTag = FGameplayTag();

	Super::EndAbility(
		Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UTopDownGameplayAbility_Move::OnInputReleased(float /*TimeHeld*/)
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UTopDownGameplayAbility_Move::OnMovementStoppedTagChanged(
	const FGameplayTag /*ChangedTag*/, int32 NewCount)
{
	if (NewCount > 0 && IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}

FGameplayTag UTopDownGameplayAbility_Move::ResolveDirectionTag(
	const FGameplayAbilitySpecHandle Handle) const
{
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo();
	const FGameplayAbilitySpec* Spec = AbilitySystem
		? AbilitySystem->FindAbilitySpecFromHandle(Handle)
		: nullptr;
	if (!Spec)
	{
		return FGameplayTag();
	}

	const FGameplayTagContainer& SourceTags = Spec->GetDynamicSpecSourceTags();
	const FGameplayTag DirectionTags[] = {
		TopDownFeatureGameplayTags::InputTag_TopDownMoveForward,
		TopDownFeatureGameplayTags::InputTag_TopDownMoveBackward,
		TopDownFeatureGameplayTags::InputTag_TopDownMoveRight,
		TopDownFeatureGameplayTags::InputTag_TopDownMoveLeft,
	};
	for (const FGameplayTag DirectionTag : DirectionTags)
	{
		if (SourceTags.HasTagExact(DirectionTag))
		{
			return DirectionTag;
		}
	}
	return FGameplayTag();
}
