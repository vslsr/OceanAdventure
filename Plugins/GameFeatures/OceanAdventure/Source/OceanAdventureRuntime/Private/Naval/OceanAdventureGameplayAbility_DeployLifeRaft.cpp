// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_DeployLifeRaft.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalMovementComponent.h"
#include "Naval/NavalRegistrySubsystem.h"
#include "Naval/NavalTeamStatics.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureNavalMatchComponent.h"
#include "Naval/OceanAdventureNavalMessages.h"
#include "Naval/OceanAdventureNavalSettings.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_DeployLifeRaft)

UOceanAdventureGameplayAbility_DeployLifeRaft::UOceanAdventureGameplayAbility_DeployLifeRaft(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

UOceanAdventureNavalMatchComponent* UOceanAdventureGameplayAbility_DeployLifeRaft::FindMatchComponent() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->FindComponentByClass<UOceanAdventureNavalMatchComponent>() : nullptr;
}

bool UOceanAdventureGameplayAbility_DeployLifeRaft::CanDeployLifeRaft(FGameplayTag& OutFailReason) const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	const int32 TeamId = NavalTeam::GetTeamId(Avatar);
	if (!NavalTeam::IsValidTeam(TeamId))
	{
		OutFailReason = NavalGameplayTags::Fail_WrongTeam;
		return false;
	}

	const UOceanAdventureNavalMatchComponent* MatchComponent = FindMatchComponent();
	if (!MatchComponent || !MatchComponent->IsLifeRaftAvailable(TeamId))
	{
		OutFailReason = NavalGameplayTags::Fail_LimitReached;
		return false;
	}

	// Only while the team has nothing left to sail. A crew with a working ship asking for a
	// second hull is exactly the snowball design 8.6 rules out.
	if (const UNavalRegistrySubsystem* Registry = UNavalRegistrySubsystem::Get(this))
	{
		TArray<UNavalVesselComponent*> Operational;
		Registry->CollectOperationalVessels(Operational);
		for (const UNavalVesselComponent* Vessel : Operational)
		{
			if (Vessel && Vessel->GetTeamId() == TeamId && !Vessel->IsLifeRaft())
			{
				OutFailReason = NavalGameplayTags::Fail_LimitReached;
				return false;
			}
		}
	}

	if (UOceanAdventureNavalSettings::Get().LifeRaftClass.IsNull())
	{
		OutFailReason = NavalGameplayTags::Fail_NotOperational;
		return false;
	}

	return true;
}

FVector UOceanAdventureGameplayAbility_DeployLifeRaft::ComputeDeployLocation() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return FVector::ZeroVector;
	}

	// Just ahead of the player and at the waterline. Buoyancy pulls it to the real surface on
	// its first update, so an approximate Z here is enough.
	FVector Location = Avatar->GetActorLocation() + Avatar->GetActorForwardVector() * DeployForwardDistance;
	Location.Z = 0.0;
	return Location;
}

void UOceanAdventureGameplayAbility_DeployLifeRaft::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bCommitSent = false;
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

	FGameplayTag FailReason;
	if (!CanDeployLifeRaft(FailReason))
	{
		BroadcastFailure(FailReason);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	PendingDeployLocation = ComputeDeployLocation();

	UAbilityTask_WaitDelay* ChannelTask = UAbilityTask_WaitDelay::WaitDelay(
		this, UOceanAdventureNavalSettings::Get().LifeRaftDeploySeconds);
	if (!ChannelTask)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ChannelTask->OnFinish.AddDynamic(this, &ThisClass::OnDeployChannelFinished);
	ChannelTask->ReadyForActivation();
}

void UOceanAdventureGameplayAbility_DeployLifeRaft::OnDeployChannelFinished()
{
	if (bCommitSent)
	{
		return;
	}
	bCommitSent = true;

	FOceanAdventureNavalTargetData DeployData;
	DeployData.Request = ENavalStationRequest::DeployLifeRaft;
	DeployData.AimLocation = PendingDeployLocation;

	FGameplayAbilityTargetDataHandle DataHandle;
	DataHandle.Add(new FOceanAdventureNavalTargetData(DeployData));
	OnTargetDataReadyCallback(DataHandle, FGameplayTag());
}

void UOceanAdventureGameplayAbility_DeployLifeRaft::OnTargetDataReadyCallback(
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
		// The channel finished on the client; whether the team still qualifies is decided here.
		FGameplayTag FailReason;
		if (Data && CanDeployLifeRaft(FailReason))
		{
			SpawnLifeRaft(Data->AimLocation);
		}
	}

	AbilitySystem->ConsumeClientReplicatedTargetData(
		CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

bool UOceanAdventureGameplayAbility_DeployLifeRaft::SpawnLifeRaft(const FVector& DeployLocation)
{
	UWorld* World = GetWorld();
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UOceanAdventureNavalMatchComponent* MatchComponent = FindMatchComponent();
	if (!World || !Avatar || !MatchComponent)
	{
		return false;
	}

	const UOceanAdventureNavalSettings& Settings = UOceanAdventureNavalSettings::Get();
	const TSubclassOf<AActor> LifeRaftClass = Settings.LifeRaftClass.LoadSynchronous();
	if (!LifeRaftClass)
	{
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[NavalLifeRaft] No LifeRaftClass configured; a team that lost its ship has no way back onto the water"));
		return false;
	}

	const int32 TeamId = NavalTeam::GetTeamId(Avatar);
	// Spend the entitlement before spawning, so a failure below cannot hand out two.
	if (!MatchComponent->ConsumeLifeRaft(TeamId))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Instigator = Cast<APawn>(Avatar);

	AActor* LifeRaft = World->SpawnActor<AActor>(
		LifeRaftClass, DeployLocation, Avatar->GetActorRotation(), SpawnParameters);
	if (!LifeRaft)
	{
		return false;
	}

	if (UNavalVesselComponent* Vessel = LifeRaft->FindComponentByClass<UNavalVesselComponent>())
	{
		Vessel->SetTeamId(TeamId);
		Vessel->ConfigureAsLifeRaft(Settings.LifeRaftHullScale);
	}
	if (UNavalMovementComponent* Movement = LifeRaft->FindComponentByClass<UNavalMovementComponent>())
	{
		Movement->SetSpeedScale(Settings.LifeRaftSpeedScale);
	}

	UE_LOG(
		LogOceanAdventure,
		Display,
		TEXT("[NavalLifeRaft] Deployed for team=%d at %s"),
		TeamId,
		*DeployLocation.ToCompactString());
	return true;
}

void UOceanAdventureGameplayAbility_DeployLifeRaft::EndAbility(
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

void UOceanAdventureGameplayAbility_DeployLifeRaft::BroadcastFailure(FGameplayTag FailReason) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FOceanAdventureNavalStationFailedMessage Message;
	Message.FailReason = FailReason;
	Message.Instigator = GetAvatarActorFromActorInfo();
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(
		OceanAdventureNavalTags::Message_Naval_StationFailed, Message);
}
