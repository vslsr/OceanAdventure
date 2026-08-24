// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureNavalProximityComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Naval/NavalRegistrySubsystem.h"
#include "Naval/OceanAdventureNavalMessages.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "OceanAdventureRuntimeModule.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureNavalProximityComponent)

UOceanAdventureNavalProximityComponent::UOceanAdventureNavalProximityComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	// Nothing here crosses the wire: each owning client works out its own prompt.
	SetIsReplicatedByDefault(false);
}

void UOceanAdventureNavalProximityComponent::BeginPlay()
{
	Super::BeginPlay();

	const APawn* Pawn = GetPawn<APawn>();
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		// A prompt only means something to the player looking at it. The server has no use
		// for this tag -- it re-runs the station's accept check on every request anyway.
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ScanTimerHandle,
			this,
			&ThisClass::ScanForStation,
			ScanInterval,
			/*bLoop=*/true);
	}
	ScanForStation();
}

void UOceanAdventureNavalProximityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ScanTimerHandle);
	}
	SetNearestStation(nullptr);

	Super::EndPlay(EndPlayReason);
}

void UOceanAdventureNavalProximityComponent::ScanForStation()
{
	APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		SetNearestStation(nullptr);
		return;
	}

	// Already on a gun: the useful prompt there is "press E to leave", which the station
	// ability owns, so this one gets out of the way rather than competing with it.
	if (IsOperatingStation())
	{
		SetNearestStation(nullptr);
		return;
	}

	UNavalRegistrySubsystem* Registry = UNavalRegistrySubsystem::Get(this);
	if (!Registry)
	{
		SetNearestStation(nullptr);
		return;
	}

	// No class filter: the wheel and the gun share one key, so "is there anything here" is
	// the question the prompt is actually asking. Passing the pawn rather than a location
	// lets each station apply its own team and occupancy rules, so an enemy gun or one
	// somebody else is already sitting at never lights up.
	const double Scale = bStationAvailable ? 1.0 : EnterRangeScale;
	SetNearestStation(Registry->FindReachableStation(Pawn, /*StationClass=*/nullptr, Scale));
}

bool UOceanAdventureNavalProximityComponent::IsOperatingStation() const
{
	const UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (!AbilitySystem)
	{
		return false;
	}

	return AbilitySystem->HasMatchingGameplayTag(OceanAdventureNavalTags::Status_Naval_OperatingHeavyWeapon)
		|| AbilitySystem->HasMatchingGameplayTag(OceanAdventureNavalTags::Status_Naval_Steering);
}

void UOceanAdventureNavalProximityComponent::SetNearestStation(AActor* Station)
{
	if (NearestStation.Get() == Station)
	{
		return;
	}

	UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (!AbilitySystem)
	{
		// Lyra's ASC lives on the PlayerState and may not have replicated yet. Leaving the
		// cached state alone means the next scan retries rather than this silently believing
		// the tag is in the state it wanted.
		return;
	}

	NearestStation = Station;

	const bool bAvailable = Station != nullptr;
	if (bAvailable != bStationAvailable)
	{
		bStationAvailable = bAvailable;
		if (bAvailable)
		{
			AbilitySystem->AddLooseGameplayTag(OceanAdventureNavalTags::Status_Naval_StationAvailable);
		}
		else
		{
			AbilitySystem->RemoveLooseGameplayTag(OceanAdventureNavalTags::Status_Naval_StationAvailable);
		}
	}

	// Sent even when only the station changed and availability did not, so a prompt that
	// names the station follows the player from one gun straight to the next.
	if (UWorld* World = GetWorld())
	{
		FOceanAdventureNavalStationPromptMessage Message;
		Message.StationActor = Station;
		Message.bAvailable = bAvailable;
		UGameplayMessageSubsystem::Get(World).BroadcastMessage(
			OceanAdventureNavalTags::Message_Naval_StationPrompt, Message);
	}

	UE_LOG(LogOceanAdventure, Verbose,
		TEXT("[NavalProximity] Prompt pawn=%s station=%s available=%d"),
		*GetNameSafe(GetOwner()), *GetNameSafe(Station), bAvailable);
}
