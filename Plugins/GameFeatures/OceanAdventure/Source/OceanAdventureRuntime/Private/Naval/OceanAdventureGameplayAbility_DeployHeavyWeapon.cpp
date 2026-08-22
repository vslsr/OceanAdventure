// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureGameplayAbility_DeployHeavyWeapon.h"

#include "AbilitySystemComponent.h"
#include "CollisionShape.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalHeavyWeaponActor.h"
#include "Naval/NavalTeamStatics.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureNavalMessages.h"
#include "Naval/OceanAdventureNavalSettings.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_DeployHeavyWeapon)

UOceanAdventureGameplayAbility_DeployHeavyWeapon::UOceanAdventureGameplayAbility_DeployHeavyWeapon(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

FVector UOceanAdventureGameplayAbility_DeployHeavyWeapon::ComputeDeployLocation() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	return Avatar
		? Avatar->GetActorLocation() + Avatar->GetActorForwardVector() * DeployForwardDistance
		: FVector::ZeroVector;
}

int32 UOceanAdventureGameplayAbility_DeployHeavyWeapon::CountTeamGroundWeapons(int32 TeamId) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}

	// A generous overlap around the player rather than a world scan: the budget only has to
	// stop one island filling up with turrets, and guns a kilometre away are not that problem.
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		ComputeDeployLocation(),
		FQuat::Identity,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(6000.0f));

	TSet<const AActor*> Counted;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const ANavalHeavyWeaponActor* Weapon = Cast<ANavalHeavyWeaponActor>(Overlap.GetActor());
		// Deck guns belong to the ship's build budget, not to the field-gun budget.
		if (!Weapon || UNavalVesselComponent::FindVessel(Weapon) != nullptr)
		{
			continue;
		}
		if (Weapon->GetTeamId() == TeamId)
		{
			Counted.Add(Weapon);
		}
	}

	return Counted.Num();
}

bool UOceanAdventureGameplayAbility_DeployHeavyWeapon::CanDeployHere(
	const FVector& Location, FGameplayTag& OutFailReason) const
{
	const UOceanAdventureNavalSettings& Settings = UOceanAdventureNavalSettings::Get();
	if (Settings.GroundHeavyWeaponClass.IsNull())
	{
		OutFailReason = NavalGameplayTags::Fail_NotOperational;
		return false;
	}

	const int32 TeamId = NavalTeam::GetTeamId(GetAvatarActorFromActorInfo());
	if (!NavalTeam::IsValidTeam(TeamId))
	{
		OutFailReason = NavalGameplayTags::Fail_WrongTeam;
		return false;
	}
	if (CountTeamGroundWeapons(TeamId) >= Settings.MaxGroundHeavyWeaponsPerTeam)
	{
		OutFailReason = NavalGameplayTags::Fail_LimitReached;
		return false;
	}

	// "No foundation required" still means the legs need ground that exists, is flat enough
	// and does not step away under one of them.
	return ANavalHeavyWeaponActor::IsGroundSuitable(
		this,
		Location,
		Settings.GroundHeavyWeaponFootprintRadius,
		Settings.GroundHeavyWeaponMaxSlopeDegrees,
		OutFailReason);
}

void UOceanAdventureGameplayAbility_DeployHeavyWeapon::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bDeployResolved = false;
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

	const FVector DeployLocation = ComputeDeployLocation();
	FGameplayTag FailReason;
	if (!CanDeployHere(DeployLocation, FailReason))
	{
		BroadcastFailure(FailReason);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FOceanAdventureNavalTargetData DeployData;
	DeployData.Request = ENavalStationRequest::DeployHeavyWeapon;
	DeployData.AimLocation = DeployLocation;

	FGameplayAbilityTargetDataHandle DataHandle;
	DataHandle.Add(new FOceanAdventureNavalTargetData(DeployData));
	OnTargetDataReadyCallback(DataHandle, FGameplayTag());
}

void UOceanAdventureGameplayAbility_DeployHeavyWeapon::OnTargetDataReadyCallback(
	const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag)
{
	UAbilitySystemComponent* AbilitySystem = CurrentActorInfo
		? CurrentActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	if (!AbilitySystem)
	{
		return;
	}

	if (bDeployResolved)
	{
		AbilitySystem->ConsumeClientReplicatedTargetData(
			CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
		return;
	}
	bDeployResolved = true;

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
		FGameplayTag FailReason;
		UWorld* World = GetWorld();
		AActor* Avatar = GetAvatarActorFromActorInfo();

		// The client picked the spot; the server decides whether that spot is legal.
		if (Data && World && Avatar && CanDeployHere(Data->AimLocation, FailReason))
		{
			const TSubclassOf<AActor> WeaponClass =
				UOceanAdventureNavalSettings::Get().GroundHeavyWeaponClass.LoadSynchronous();
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParameters.Instigator = Cast<APawn>(Avatar);

			// Spawned as an AActor and cast afterwards: a misconfigured class in project
			// settings should log and do nothing, not assert inside SpawnActor.
			AActor* Spawned = World->SpawnActor<AActor>(
				WeaponClass, Data->AimLocation, Avatar->GetActorRotation(), SpawnParameters);
			if (ANavalHeavyWeaponActor* Weapon = Cast<ANavalHeavyWeaponActor>(Spawned))
			{
				// Deploy windup, then a construction ramp during which it can be broken and
				// cannot fire. Setting up in a fight is meant to be a visible risk.
				Weapon->BeginDeployment(NavalTeam::GetTeamId(Avatar));
			}
			else if (Spawned)
			{
				UE_LOG(
					LogOceanAdventure,
					Warning,
					TEXT("[NavalDeploy] GroundHeavyWeaponClass %s is not a heavy weapon"),
					*GetNameSafe(Spawned->GetClass()));
				Spawned->Destroy();
			}
		}
	}

	AbilitySystem->ConsumeClientReplicatedTargetData(
		CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UOceanAdventureGameplayAbility_DeployHeavyWeapon::EndAbility(
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

void UOceanAdventureGameplayAbility_DeployHeavyWeapon::BroadcastFailure(FGameplayTag FailReason) const
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
