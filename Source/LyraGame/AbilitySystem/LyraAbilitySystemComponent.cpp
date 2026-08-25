// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraAbilitySystemComponent.h"

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "AbilitySystem/LyraAbilityTagRelationshipMapping.h"
#include "Animation/LyraAnimInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "LyraGlobalAbilitySystem.h"
#include "LyraLogChannels.h"
#include "System/LyraAssetManager.h"
#include "System/LyraGameData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraAbilitySystemComponent)

UE_DEFINE_GAMEPLAY_TAG(TAG_Gameplay_AbilityInputBlocked, "Gameplay.AbilityInputBlocked");

ULyraAbilitySystemComponent::ULyraAbilitySystemComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.Reset();
	DiagnosticLastInputHadMatchingSpec.Reset();
	DiagnosticInputEventSerial = 0;

	FMemory::Memset(ActivationGroupCounts, 0, sizeof(ActivationGroupCounts));
}

void ULyraAbilitySystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ULyraGlobalAbilitySystem* GlobalAbilitySystem = UWorld::GetSubsystem<ULyraGlobalAbilitySystem>(GetWorld()))
	{
		GlobalAbilitySystem->UnregisterASC(this);
	}

	Super::EndPlay(EndPlayReason);
}

void ULyraAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	FGameplayAbilityActorInfo* ActorInfo = AbilityActorInfo.Get();
	check(ActorInfo);
	check(InOwnerActor);

	const bool bHasNewPawnAvatar = Cast<APawn>(InAvatarActor) && (InAvatarActor != ActorInfo->AvatarActor);

	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);

	if (bHasNewPawnAvatar)
	{
		// Notify all abilities that a new pawn avatar has been set
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			ensureMsgf(AbilitySpec.Ability && AbilitySpec.Ability->GetInstancingPolicy() != EGameplayAbilityInstancingPolicy::NonInstanced, TEXT("InitAbilityActorInfo: All Abilities should be Instanced (NonInstanced is being deprecated due to usability issues)."));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	
			TArray<UGameplayAbility*> Instances = AbilitySpec.GetAbilityInstances();
			for (UGameplayAbility* AbilityInstance : Instances)
			{
				ULyraGameplayAbility* LyraAbilityInstance = Cast<ULyraGameplayAbility>(AbilityInstance);
				if (LyraAbilityInstance)
				{
					// Ability instances may be missing for replays
					LyraAbilityInstance->OnPawnAvatarSet();
				}
			}
		}

		// Register with the global system once we actually have a pawn avatar. We wait until this time since some globally-applied effects may require an avatar.
		if (ULyraGlobalAbilitySystem* GlobalAbilitySystem = UWorld::GetSubsystem<ULyraGlobalAbilitySystem>(GetWorld()))
		{
			GlobalAbilitySystem->RegisterASC(this);
		}

		if (ULyraAnimInstance* LyraAnimInst = Cast<ULyraAnimInstance>(ActorInfo->GetAnimInstance()))
		{
			LyraAnimInst->InitializeWithAbilitySystem(this);
		}

		TryActivateAbilitiesOnSpawn();
	}
}

void ULyraAbilitySystemComponent::TryActivateAbilitiesOnSpawn()
{
	ABILITYLIST_SCOPE_LOCK();
	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (const ULyraGameplayAbility* LyraAbilityCDO = Cast<ULyraGameplayAbility>(AbilitySpec.Ability))
		{
			LyraAbilityCDO->TryActivateAbilityOnSpawn(AbilityActorInfo.Get(), AbilitySpec);
		}
	}
}

void ULyraAbilitySystemComponent::CancelAbilitiesByFunc(TShouldCancelAbilityFunc ShouldCancelFunc, bool bReplicateCancelAbility)
{
	ABILITYLIST_SCOPE_LOCK();
	for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.IsActive())
		{
			continue;
		}

		ULyraGameplayAbility* LyraAbilityCDO = Cast<ULyraGameplayAbility>(AbilitySpec.Ability);
		if (!LyraAbilityCDO)
		{
			UE_LOG(LogLyraAbilitySystem, Error, TEXT("CancelAbilitiesByFunc: Non-LyraGameplayAbility %s was Granted to ASC. Skipping."), *AbilitySpec.Ability.GetName());
			continue;
		}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
		ensureMsgf(AbilitySpec.Ability->GetInstancingPolicy() != EGameplayAbilityInstancingPolicy::NonInstanced, TEXT("CancelAbilitiesByFunc: All Abilities should be Instanced (NonInstanced is being deprecated due to usability issues)."));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			
		// Cancel all the spawned instances.
		TArray<UGameplayAbility*> Instances = AbilitySpec.GetAbilityInstances();
		for (UGameplayAbility* AbilityInstance : Instances)
		{
			ULyraGameplayAbility* LyraAbilityInstance = CastChecked<ULyraGameplayAbility>(AbilityInstance);

			if (ShouldCancelFunc(LyraAbilityInstance, AbilitySpec.Handle))
			{
				if (LyraAbilityInstance->CanBeCanceled())
				{
					LyraAbilityInstance->CancelAbility(AbilitySpec.Handle, AbilityActorInfo.Get(), LyraAbilityInstance->GetCurrentActivationInfo(), bReplicateCancelAbility);
				}
				else
				{
					UE_LOG(LogLyraAbilitySystem, Error, TEXT("CancelAbilitiesByFunc: Can't cancel ability [%s] because CanBeCanceled is false."), *LyraAbilityInstance->GetName());
				}
			}
		}
	}
}

void ULyraAbilitySystemComponent::CancelInputActivatedAbilities(bool bReplicateCancelAbility)
{
	auto ShouldCancelFunc = [this](const ULyraGameplayAbility* LyraAbility, FGameplayAbilitySpecHandle Handle)
	{
		const ELyraAbilityActivationPolicy ActivationPolicy = LyraAbility->GetActivationPolicy();
		return ((ActivationPolicy == ELyraAbilityActivationPolicy::OnInputTriggered) || (ActivationPolicy == ELyraAbilityActivationPolicy::WhileInputActive));
	};

	CancelAbilitiesByFunc(ShouldCancelFunc, bReplicateCancelAbility);
}

void ULyraAbilitySystemComponent::AbilitySpecInputPressed(FGameplayAbilitySpec& Spec)
{
	Super::AbilitySpecInputPressed(Spec);
	if (Spec.GetDynamicSpecSourceTags().ToStringSimple().Contains(TEXT("InputTag.Naval")))
	{
		UE_LOG(
			LogLyraAbilitySystem,
			Display,
			TEXT("[AbilityInput] ASC active-spec press-dispatch serial=%llu spec=%s ability=%s source=%s input_pressed=%d active=%d avatar=%s world=%.3f"),
			static_cast<unsigned long long>(++DiagnosticInputEventSerial),
			*Spec.Handle.ToString(), *GetNameSafe(Spec.Ability.Get()),
			*GetNameSafe(Spec.SourceObject.Get()), Spec.InputPressed, Spec.IsActive(),
			*GetNameSafe(GetAvatarActor()), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
	}

	// We don't support UGameplayAbility::bReplicateInputDirectly.
	// Use replicated events instead so that the WaitInputPress ability task works.
	if (Spec.IsActive())
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const UGameplayAbility* Instance = Spec.GetPrimaryInstance();
		FPredictionKey OriginalPredictionKey = Instance ? Instance->GetCurrentActivationInfo().GetActivationPredictionKey() : Spec.ActivationInfo.GetActivationPredictionKey();
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		// Invoke the InputPressed event. This is not replicated here. If someone is listening, they may replicate the InputPressed event to the server.
		InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Spec.Handle, OriginalPredictionKey);
	}
}

void ULyraAbilitySystemComponent::AbilitySpecInputReleased(FGameplayAbilitySpec& Spec)
{
	Super::AbilitySpecInputReleased(Spec);
	if (Spec.GetDynamicSpecSourceTags().ToStringSimple().Contains(TEXT("InputTag.Naval")))
	{
		UE_LOG(
			LogLyraAbilitySystem,
			Display,
			TEXT("[AbilityInput] ASC active-spec release-dispatch serial=%llu spec=%s ability=%s source=%s input_pressed=%d active=%d avatar=%s world=%.3f"),
			static_cast<unsigned long long>(++DiagnosticInputEventSerial),
			*Spec.Handle.ToString(), *GetNameSafe(Spec.Ability.Get()),
			*GetNameSafe(Spec.SourceObject.Get()), Spec.InputPressed, Spec.IsActive(),
			*GetNameSafe(GetAvatarActor()), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
	}

	// We don't support UGameplayAbility::bReplicateInputDirectly.
	// Use replicated events instead so that the WaitInputRelease ability task works.
	if (Spec.IsActive())
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const UGameplayAbility* Instance = Spec.GetPrimaryInstance();
		FPredictionKey OriginalPredictionKey = Instance ? Instance->GetCurrentActivationInfo().GetActivationPredictionKey() : Spec.ActivationInfo.GetActivationPredictionKey();
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		// Invoke the InputReleased event. This is not replicated here. If someone is listening, they may replicate the InputReleased event to the server.
		InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Spec.Handle, OriginalPredictionKey);
	}
}

void ULyraAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	if (InputTag.IsValid())
	{
		const uint64 EventSerial = ++DiagnosticInputEventSerial;
		const bool bBuildInput = InputTag.ToString().StartsWith(TEXT("InputTag.Build"));
		const bool bNavalInput = InputTag.ToString().StartsWith(TEXT("InputTag.Naval"));
		const bool bFireInput = InputTag.ToString().Equals(TEXT("InputTag.Naval.Fire"), ESearchCase::CaseSensitive);
		const bool bDiagnosticInput = bBuildInput || bNavalInput;
		int32 MatchingSpecs = 0;
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
			if (AbilitySpec.Ability && (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag)))
			{
				++MatchingSpecs;
				InputPressedSpecHandles.AddUnique(AbilitySpec.Handle);
				InputHeldSpecHandles.AddUnique(AbilitySpec.Handle);
				if (bDiagnosticInput)
				{
					const ULyraGameplayAbility* LyraAbility = Cast<ULyraGameplayAbility>(AbilitySpec.Ability);
					UE_LOG(
						LogLyraAbilitySystem,
						Display,
						TEXT("[AbilityInput] ASC matched serial=%llu tag=%s spec=%s ability=%s source=%s active=%d policy=%d input_pressed=%d avatar=%s world=%.3f"),
						static_cast<unsigned long long>(EventSerial),
						*InputTag.ToString(),
						*AbilitySpec.Handle.ToString(),
						*GetNameSafe(AbilitySpec.Ability),
						*GetNameSafe(AbilitySpec.SourceObject.Get()),
						AbilitySpec.IsActive(),
						LyraAbility ? static_cast<int32>(LyraAbility->GetActivationPolicy()) : INDEX_NONE,
						AbilitySpec.InputPressed,
						*GetNameSafe(GetAvatarActor()), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
				}
			}
		}
		const bool bHadMatchingSpec = MatchingSpecs > 0;
		const bool bPreviousHadMatchingSpec = DiagnosticLastInputHadMatchingSpec.FindRef(InputTag);
		const bool bMatchStateChanged = !DiagnosticLastInputHadMatchingSpec.Contains(InputTag)
			|| bPreviousHadMatchingSpec != bHadMatchingSpec;
		DiagnosticLastInputHadMatchingSpec.Add(InputTag, bHadMatchingSpec);
		if (bDiagnosticInput)
		{
			UE_LOG(
				LogLyraAbilitySystem,
				Display,
				TEXT("[AbilityInput] ASC press summary serial=%llu tag=%s matching_specs=%d pressed_queue=%d held_queue=%d blocked=%d avatar=%s owner_role=%d world=%.3f"),
				static_cast<unsigned long long>(EventSerial),
				*InputTag.ToString(),
				MatchingSpecs,
				InputPressedSpecHandles.Num(),
				InputHeldSpecHandles.Num(),
				HasMatchingGameplayTag(TAG_Gameplay_AbilityInputBlocked),
				*GetNameSafe(GetAvatarActor()),
				GetAvatarActor() ? GetAvatarActor()->GetLocalRole() : ROLE_None,
				GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);

			if (bNavalInput && bMatchStateChanged)
			{
				UE_LOG(
					LogLyraAbilitySystem,
					Warning,
					TEXT("[AbilityInput] NAVAL match-state transition serial=%llu tag=%s had_matching_spec=%d avatar=%s"),
					static_cast<unsigned long long>(EventSerial), *InputTag.ToString(), bHadMatchingSpec,
					*GetNameSafe(GetAvatarActor()));
				if (!bHadMatchingSpec)
				{
					for (const FGameplayAbilitySpec& AvailableSpec : ActivatableAbilities.Items)
					{
						UE_LOG(
							LogLyraAbilitySystem,
							Warning,
							TEXT("[AbilityInput] NAVAL available-spec serial=%llu spec=%s ability=%s tags=%s source=%s active=%d input_pressed=%d"),
							static_cast<unsigned long long>(EventSerial),
							*AvailableSpec.Handle.ToString(), *GetNameSafe(AvailableSpec.Ability.Get()),
							*AvailableSpec.GetDynamicSpecSourceTags().ToStringSimple(),
							*GetNameSafe(AvailableSpec.SourceObject.Get()), AvailableSpec.IsActive(),
							AvailableSpec.InputPressed);
					}
				}
			}
			if (bFireInput && bHadMatchingSpec && bMatchStateChanged)
			{
				UE_LOG(
					LogLyraAbilitySystem,
					Display,
					TEXT("[NavalFire] FIRE_SPEC_AVAILABLE serial=%llu avatar=%s world=%.3f"),
					static_cast<unsigned long long>(EventSerial), *GetNameSafe(GetAvatarActor()),
					GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
			}
			if (bFireInput && MatchingSpecs == 0 && bMatchStateChanged)
			{
				UE_LOG(
					LogLyraAbilitySystem,
					Error,
					TEXT("[NavalFire] FIRE_INPUT_NO_SPEC serial=%llu tag=%s avatar=%s world=%.3f"),
					static_cast<unsigned long long>(EventSerial), *InputTag.ToString(),
					*GetNameSafe(GetAvatarActor()), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
			}
		}
	}
}

void ULyraAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	if (InputTag.IsValid())
	{
		const uint64 EventSerial = ++DiagnosticInputEventSerial;
		const bool bNavalInput = InputTag.ToString().StartsWith(TEXT("InputTag.Naval"));
		const bool bFireInput = InputTag.ToString().Equals(TEXT("InputTag.Naval.Fire"), ESearchCase::CaseSensitive);
		int32 MatchingSpecs = 0;
		for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
		{
			if (AbilitySpec.Ability && (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag)))
			{
				++MatchingSpecs;
				InputReleasedSpecHandles.AddUnique(AbilitySpec.Handle);
				InputHeldSpecHandles.Remove(AbilitySpec.Handle);
			}
		}
		if (bNavalInput)
		{
			UE_LOG(LogLyraAbilitySystem, Display,
				TEXT("[AbilityInput] ASC release serial=%llu tag=%s matching_specs=%d released_queue=%d held_queue=%d avatar=%s owner_role=%d world=%.3f"),
				static_cast<unsigned long long>(EventSerial),
				*InputTag.ToString(), MatchingSpecs, InputReleasedSpecHandles.Num(),
				InputHeldSpecHandles.Num(), *GetNameSafe(GetAvatarActor()),
				GetAvatarActor() ? GetAvatarActor()->GetLocalRole() : ROLE_None,
				GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
			if (bFireInput && MatchingSpecs == 0)
			{
				UE_LOG(
					LogLyraAbilitySystem,
					Error,
					TEXT("[NavalFire] FIRE_RELEASE_NO_SPEC serial=%llu tag=%s avatar=%s world=%.3f"),
					static_cast<unsigned long long>(EventSerial), *InputTag.ToString(),
					*GetNameSafe(GetAvatarActor()), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
			}
		}
	}
}

void ULyraAbilitySystemComponent::ProcessAbilityInput(float DeltaTime, bool bGamePaused)
{
	if (HasMatchingGameplayTag(TAG_Gameplay_AbilityInputBlocked))
	{
		if (InputPressedSpecHandles.Num() > 0 || InputReleasedSpecHandles.Num() > 0 || InputHeldSpecHandles.Num() > 0)
		{
			UE_LOG(
				LogLyraAbilitySystem,
				Warning,
				TEXT("[AbilityInput] ASC input blocked; clearing queues pressed=%d released=%d held=%d avatar=%s world=%.3f"),
				InputPressedSpecHandles.Num(), InputReleasedSpecHandles.Num(), InputHeldSpecHandles.Num(),
				*GetNameSafe(GetAvatarActor()), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
		}
		ClearAbilityInput();
		return;
	}

	static TArray<FGameplayAbilitySpecHandle> AbilitiesToActivate;
	AbilitiesToActivate.Reset();

	//@TODO: See if we can use FScopedServerAbilityRPCBatcher ScopedRPCBatcher in some of these loops

	//
	// Process all abilities that activate when the input is held.
	//
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputHeldSpecHandles)
	{
		if (const FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle))
		{
			if (AbilitySpec->Ability && !AbilitySpec->IsActive())
			{
				const ULyraGameplayAbility* LyraAbilityCDO = Cast<ULyraGameplayAbility>(AbilitySpec->Ability);
				if (LyraAbilityCDO && LyraAbilityCDO->GetActivationPolicy() == ELyraAbilityActivationPolicy::WhileInputActive)
				{
					AbilitiesToActivate.AddUnique(AbilitySpec->Handle);
				}
			}
		}
	}

	//
	// Process all abilities that had their input pressed this frame.
	//
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputPressedSpecHandles)
	{
		if (FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle))
		{
			if (AbilitySpec->Ability)
			{
				AbilitySpec->InputPressed = true;

				if (AbilitySpec->IsActive())
				{
					// Ability is active so pass along the input event.
					AbilitySpecInputPressed(*AbilitySpec);
				}
				else
				{
					const ULyraGameplayAbility* LyraAbilityCDO = Cast<ULyraGameplayAbility>(AbilitySpec->Ability);

					if (LyraAbilityCDO && LyraAbilityCDO->GetActivationPolicy() == ELyraAbilityActivationPolicy::OnInputTriggered)
					{
						AbilitiesToActivate.AddUnique(AbilitySpec->Handle);
					}
				}
			}
		}
	}

	//
	// Try to activate all the abilities that are from presses and holds.
	// We do it all at once so that held inputs don't activate the ability
	// and then also send a input event to the ability because of the press.
	//
	for (const FGameplayAbilitySpecHandle& AbilitySpecHandle : AbilitiesToActivate)
	{
		const FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(AbilitySpecHandle);
		const FString SpecInputTags = AbilitySpec
			? AbilitySpec->GetDynamicSpecSourceTags().ToStringSimple()
			: FString();
		const bool bBuildAbility = SpecInputTags.Contains(TEXT("InputTag.Build"));
		const bool bNavalAbility = SpecInputTags.Contains(TEXT("InputTag.Naval"));
		const bool bActivated = TryActivateAbility(AbilitySpecHandle);
		if (bBuildAbility || bNavalAbility)
		{
			UE_LOG(
				LogLyraAbilitySystem,
				Display,
				TEXT("[AbilityInput] ASC activation result tags=%s spec=%s ability=%s source=%s active_after=%d input_pressed=%d activated=%d avatar=%s world=%.3f"),
				*SpecInputTags,
				*AbilitySpecHandle.ToString(),
				*GetNameSafe(AbilitySpec ? AbilitySpec->Ability.Get() : nullptr),
				*GetNameSafe(AbilitySpec ? AbilitySpec->SourceObject.Get() : nullptr),
				AbilitySpec ? AbilitySpec->IsActive() : false,
				AbilitySpec ? AbilitySpec->InputPressed : false,
				bActivated,
				*GetNameSafe(GetAvatarActor()), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
		}
	}

	//
	// Process all abilities that had their input released this frame.
	//
	for (const FGameplayAbilitySpecHandle& SpecHandle : InputReleasedSpecHandles)
	{
		if (FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle))
		{
			if (AbilitySpec->Ability)
			{
				const bool bInputPressedBeforeRelease = AbilitySpec->InputPressed;
				AbilitySpec->InputPressed = false;

				const FString SpecInputTags = AbilitySpec->GetDynamicSpecSourceTags().ToStringSimple();
				const bool bActiveOnRelease = AbilitySpec->IsActive();
				if (SpecInputTags.Contains(TEXT("InputTag.Naval")))
				{
					// A release that lands while the spec is inactive is dropped here: the
					// ability never sees InputReleased, so a hold-to-charge action would sit
					// waiting for a release that already happened.
					UE_LOG(
						LogLyraAbilitySystem,
						Display,
						TEXT("[AbilityInput] ASC release dispatch tags=%s ability=%s source=%s active=%d input_pressed_before=%d avatar=%s world=%.3f"),
						*SpecInputTags,
						*GetNameSafe(AbilitySpec->Ability.Get()),
						*GetNameSafe(AbilitySpec->SourceObject.Get()),
						bActiveOnRelease,
						bInputPressedBeforeRelease,
						*GetNameSafe(GetAvatarActor()), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
				}

				if (bActiveOnRelease)
				{
					// Ability is active so pass along the input event.
					AbilitySpecInputReleased(*AbilitySpec);
				}
			}
			else if (InputReleasedSpecHandles.Contains(SpecHandle))
			{
				UE_LOG(
					LogLyraAbilitySystem,
					Warning,
					TEXT("[AbilityInput] ASC release dropped because spec handle is missing handle=%s avatar=%s world=%.3f"),
					*SpecHandle.ToString(), *GetNameSafe(GetAvatarActor()),
					GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
			}
		}
	}

	//
	// Clear the cached ability handles.
	//
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
}

void ULyraAbilitySystemComponent::ClearAbilityInput()
{
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.Reset();
}

void ULyraAbilitySystemComponent::NotifyAbilityActivated(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability)
{
	Super::NotifyAbilityActivated(Handle, Ability);
	if (Ability && Ability->GetName().Contains(TEXT("FireHeavyWeapon")))
	{
		const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		UE_LOG(
			LogLyraAbilitySystem,
			Display,
			TEXT("[AbilityInput] ASC NotifyAbilityActivated fire handle=%s ability=%s source=%s avatar=%s world=%.3f"),
			*Handle.ToString(), *GetNameSafe(Ability),
			*GetNameSafe(Spec ? Spec->SourceObject.Get() : nullptr), *GetNameSafe(GetAvatarActor()),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
	}

	if (ULyraGameplayAbility* LyraAbility = Cast<ULyraGameplayAbility>(Ability))
	{
		AddAbilityToActivationGroup(LyraAbility->GetActivationGroup(), LyraAbility);
	}
}

void ULyraAbilitySystemComponent::NotifyAbilityFailed(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason)
{
	Super::NotifyAbilityFailed(Handle, Ability, FailureReason);
	const FString AbilityName = GetNameSafe(Ability);
	if (AbilityName.Contains(TEXT("Naval")) || AbilityName.Contains(TEXT("FireHeavyWeapon")))
	{
		const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		const FString SpecTags = Spec ? Spec->GetDynamicSpecSourceTags().ToStringSimple() : FString();
		UE_LOG(
			LogLyraAbilitySystem,
			Warning,
			TEXT("[AbilityInput] ASC NotifyAbilityFailed handle=%s ability=%s reason=%s tags=%s source=%s avatar=%s world=%.3f"),
			*Handle.ToString(), *AbilityName, *FailureReason.ToStringSimple(), *SpecTags,
			*GetNameSafe(Spec ? Spec->SourceObject.Get() : nullptr), *GetNameSafe(GetAvatarActor()),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
	}

	if (APawn* Avatar = Cast<APawn>(GetAvatarActor()))
	{
		if (Ability && !Avatar->IsLocallyControlled() && Ability->IsSupportedForNetworking())
		{
			ClientNotifyAbilityFailed(Ability, FailureReason);
			return;
		}
	}

	HandleAbilityFailed(Ability, FailureReason);
}

void ULyraAbilitySystemComponent::NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled)
{
	Super::NotifyAbilityEnded(Handle, Ability, bWasCancelled);

	if (ULyraGameplayAbility* LyraAbility = Cast<ULyraGameplayAbility>(Ability))
	{
		RemoveAbilityFromActivationGroup(LyraAbility->GetActivationGroup(), LyraAbility);
	}
}

void ULyraAbilitySystemComponent::ApplyAbilityBlockAndCancelTags(const FGameplayTagContainer& AbilityTags, UGameplayAbility* RequestingAbility, bool bEnableBlockTags, const FGameplayTagContainer& BlockTags, bool bExecuteCancelTags, const FGameplayTagContainer& CancelTags)
{
	FGameplayTagContainer ModifiedBlockTags = BlockTags;
	FGameplayTagContainer ModifiedCancelTags = CancelTags;

	if (TagRelationshipMapping)
	{
		// Use the mapping to expand the ability tags into block and cancel tag
		TagRelationshipMapping->GetAbilityTagsToBlockAndCancel(AbilityTags, &ModifiedBlockTags, &ModifiedCancelTags);
	}

	Super::ApplyAbilityBlockAndCancelTags(AbilityTags, RequestingAbility, bEnableBlockTags, ModifiedBlockTags, bExecuteCancelTags, ModifiedCancelTags);

	//@TODO: Apply any special logic like blocking input or movement
}

void ULyraAbilitySystemComponent::HandleChangeAbilityCanBeCanceled(const FGameplayTagContainer& AbilityTags, UGameplayAbility* RequestingAbility, bool bCanBeCanceled)
{
	Super::HandleChangeAbilityCanBeCanceled(AbilityTags, RequestingAbility, bCanBeCanceled);

	//@TODO: Apply any special logic like blocking input or movement
}

void ULyraAbilitySystemComponent::GetAdditionalActivationTagRequirements(const FGameplayTagContainer& AbilityTags, FGameplayTagContainer& OutActivationRequired, FGameplayTagContainer& OutActivationBlocked) const
{
	if (TagRelationshipMapping)
	{
		TagRelationshipMapping->GetRequiredAndBlockedActivationTags(AbilityTags, &OutActivationRequired, &OutActivationBlocked);
	}
}

void ULyraAbilitySystemComponent::SetTagRelationshipMapping(ULyraAbilityTagRelationshipMapping* NewMapping)
{
	TagRelationshipMapping = NewMapping;
}

void ULyraAbilitySystemComponent::ClientNotifyAbilityFailed_Implementation(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason)
{
	HandleAbilityFailed(Ability, FailureReason);
}

void ULyraAbilitySystemComponent::HandleAbilityFailed(const UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason)
{
	//UE_LOG(LogLyraAbilitySystem, Warning, TEXT("Ability %s failed to activate (tags: %s)"), *GetPathNameSafe(Ability), *FailureReason.ToString());

	if (const ULyraGameplayAbility* LyraAbility = Cast<const ULyraGameplayAbility>(Ability))
	{
		LyraAbility->OnAbilityFailedToActivate(FailureReason);
	}	
}

bool ULyraAbilitySystemComponent::IsActivationGroupBlocked(ELyraAbilityActivationGroup Group) const
{
	bool bBlocked = false;

	switch (Group)
	{
	case ELyraAbilityActivationGroup::Independent:
		// Independent abilities are never blocked.
		bBlocked = false;
		break;

	case ELyraAbilityActivationGroup::Exclusive_Replaceable:
	case ELyraAbilityActivationGroup::Exclusive_Blocking:
		// Exclusive abilities can activate if nothing is blocking.
		bBlocked = (ActivationGroupCounts[(uint8)ELyraAbilityActivationGroup::Exclusive_Blocking] > 0);
		break;

	default:
		checkf(false, TEXT("IsActivationGroupBlocked: Invalid ActivationGroup [%d]\n"), (uint8)Group);
		break;
	}

	return bBlocked;
}

void ULyraAbilitySystemComponent::AddAbilityToActivationGroup(ELyraAbilityActivationGroup Group, ULyraGameplayAbility* LyraAbility)
{
	check(LyraAbility);
	check(ActivationGroupCounts[(uint8)Group] < INT32_MAX);

	ActivationGroupCounts[(uint8)Group]++;

	const bool bReplicateCancelAbility = false;

	switch (Group)
	{
	case ELyraAbilityActivationGroup::Independent:
		// Independent abilities do not cancel any other abilities.
		break;

	case ELyraAbilityActivationGroup::Exclusive_Replaceable:
	case ELyraAbilityActivationGroup::Exclusive_Blocking:
		CancelActivationGroupAbilities(ELyraAbilityActivationGroup::Exclusive_Replaceable, LyraAbility, bReplicateCancelAbility);
		break;

	default:
		checkf(false, TEXT("AddAbilityToActivationGroup: Invalid ActivationGroup [%d]\n"), (uint8)Group);
		break;
	}

	const int32 ExclusiveCount = ActivationGroupCounts[(uint8)ELyraAbilityActivationGroup::Exclusive_Replaceable] + ActivationGroupCounts[(uint8)ELyraAbilityActivationGroup::Exclusive_Blocking];
	if (!ensure(ExclusiveCount <= 1))
	{
		UE_LOG(LogLyraAbilitySystem, Error, TEXT("AddAbilityToActivationGroup: Multiple exclusive abilities are running."));
	}
}

void ULyraAbilitySystemComponent::RemoveAbilityFromActivationGroup(ELyraAbilityActivationGroup Group, ULyraGameplayAbility* LyraAbility)
{
	check(LyraAbility);
	check(ActivationGroupCounts[(uint8)Group] > 0);

	ActivationGroupCounts[(uint8)Group]--;
}

void ULyraAbilitySystemComponent::CancelActivationGroupAbilities(ELyraAbilityActivationGroup Group, ULyraGameplayAbility* IgnoreLyraAbility, bool bReplicateCancelAbility)
{
	auto ShouldCancelFunc = [this, Group, IgnoreLyraAbility](const ULyraGameplayAbility* LyraAbility, FGameplayAbilitySpecHandle Handle)
	{
		return ((LyraAbility->GetActivationGroup() == Group) && (LyraAbility != IgnoreLyraAbility));
	};

	CancelAbilitiesByFunc(ShouldCancelFunc, bReplicateCancelAbility);
}

void ULyraAbilitySystemComponent::AddDynamicTagGameplayEffect(const FGameplayTag& Tag)
{
	const TSubclassOf<UGameplayEffect> DynamicTagGE = ULyraAssetManager::GetSubclass(ULyraGameData::Get().DynamicTagGameplayEffect);
	if (!DynamicTagGE)
	{
		UE_LOG(LogLyraAbilitySystem, Warning, TEXT("AddDynamicTagGameplayEffect: Unable to find DynamicTagGameplayEffect [%s]."), *ULyraGameData::Get().DynamicTagGameplayEffect.GetAssetName());
		return;
	}

	const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingSpec(DynamicTagGE, 1.0f, MakeEffectContext());
	FGameplayEffectSpec* Spec = SpecHandle.Data.Get();

	if (!Spec)
	{
		UE_LOG(LogLyraAbilitySystem, Warning, TEXT("AddDynamicTagGameplayEffect: Unable to make outgoing spec for [%s]."), *GetNameSafe(DynamicTagGE));
		return;
	}

	Spec->DynamicGrantedTags.AddTag(Tag);

	ApplyGameplayEffectSpecToSelf(*Spec);
}

void ULyraAbilitySystemComponent::RemoveDynamicTagGameplayEffect(const FGameplayTag& Tag)
{
	const TSubclassOf<UGameplayEffect> DynamicTagGE = ULyraAssetManager::GetSubclass(ULyraGameData::Get().DynamicTagGameplayEffect);
	if (!DynamicTagGE)
	{
		UE_LOG(LogLyraAbilitySystem, Warning, TEXT("RemoveDynamicTagGameplayEffect: Unable to find gameplay effect [%s]."), *ULyraGameData::Get().DynamicTagGameplayEffect.GetAssetName());
		return;
	}

	FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag));
	Query.EffectDefinition = DynamicTagGE;

	RemoveActiveEffects(Query);
}

void ULyraAbilitySystemComponent::GetAbilityTargetData(const FGameplayAbilitySpecHandle AbilityHandle, FGameplayAbilityActivationInfo ActivationInfo, FGameplayAbilityTargetDataHandle& OutTargetDataHandle)
{
	TSharedPtr<FAbilityReplicatedDataCache> ReplicatedData = AbilityTargetDataMap.Find(FGameplayAbilitySpecHandleAndPredictionKey(AbilityHandle, ActivationInfo.GetActivationPredictionKey()));
	if (ReplicatedData.IsValid())
	{
		OutTargetDataHandle = ReplicatedData->TargetData;
	}
}

