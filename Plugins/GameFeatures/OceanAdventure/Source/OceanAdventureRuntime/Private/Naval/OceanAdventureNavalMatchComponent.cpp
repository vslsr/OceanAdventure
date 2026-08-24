// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureNavalMatchComponent.h"

#include "Character/LyraHealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Naval/NavalRegistrySubsystem.h"
#include "Naval/NavalTeamStatics.h"
#include "Naval/NavalTimeStatics.h"
#include "Naval/NavalVesselComponent.h"
#include "Naval/OceanAdventureNavalBeaconActor.h"
#include "Naval/OceanAdventureNavalReconnectComponent.h"
#include "Naval/OceanAdventureNavalTags.h"
#include "Net/UnrealNetwork.h"
#include "OceanAdventureRuntimeModule.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureNavalMatchComponent)

UOceanAdventureNavalMatchComponent::UOceanAdventureNavalMatchComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UOceanAdventureNavalMatchComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		BroadcastMatchState();
		return;
	}

	MatchStartServerTime = NavalTime::GetNetworkTimeSeconds(this);
	SetMatchPhase(OceanAdventureNavalTags::Phase_NavalP0_Skirmish);

	// One timer drives the whole script. Everything it checks -- the clock, who is still
	// alive, the beacon -- is cheap at this rate and needs no per-frame work.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			EvaluationTimerHandle,
			this,
			&UOceanAdventureNavalMatchComponent::EvaluateMatch,
			FMath::Max(0.1f, EvaluationInterval),
			true);
	}
}

void UOceanAdventureNavalMatchComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EvaluationTimerHandle);
	}
	if (AOceanAdventureNavalBeaconActor* BeaconActor = Beacon.Get(); BeaconActor && BeaconCapturedHandle.IsValid())
	{
		BeaconActor->OnBeaconCaptured.Remove(BeaconCapturedHandle);
	}
	BeaconCapturedHandle.Reset();

	Super::EndPlay(EndPlayReason);
}

void UOceanAdventureNavalMatchComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOceanAdventureNavalMatchComponent, MatchStartServerTime);
	DOREPLIFETIME(UOceanAdventureNavalMatchComponent, CurrentPhaseTag);
	DOREPLIFETIME(UOceanAdventureNavalMatchComponent, Outcome);
	DOREPLIFETIME(UOceanAdventureNavalMatchComponent, WinningTeamId);
	DOREPLIFETIME(UOceanAdventureNavalMatchComponent, bMatchOver);
	DOREPLIFETIME(UOceanAdventureNavalMatchComponent, LifeRaftSpentTeams);
}

float UOceanAdventureNavalMatchComponent::GetElapsedSeconds() const
{
	return static_cast<float>(FMath::Max(0.0, NavalTime::GetNetworkTimeSeconds(this) - MatchStartServerTime));
}

float UOceanAdventureNavalMatchComponent::GetRemainingSeconds() const
{
	return FMath::Max(0.0f, HardTimeLimitSeconds - GetElapsedSeconds());
}

AOceanAdventureNavalBeaconActor* UOceanAdventureNavalMatchComponent::ResolveBeacon()
{
	if (AOceanAdventureNavalBeaconActor* Existing = Beacon.Get())
	{
		return Existing;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Once, from the evaluation timer, until it is found -- never per frame. The beacon is a
	// level actor, so its BeginPlay order relative to the game state is not guaranteed.
	for (TActorIterator<AOceanAdventureNavalBeaconActor> It(World); It; ++It)
	{
		Beacon = *It;
		BeaconCapturedHandle = It->OnBeaconCaptured.AddUObject(
			this, &UOceanAdventureNavalMatchComponent::HandleBeaconCaptured);
		return *It;
	}

	return nullptr;
}

void UOceanAdventureNavalMatchComponent::SetMatchPhase(FGameplayTag PhaseTag)
{
	if (CurrentPhaseTag == PhaseTag)
	{
		return;
	}

	CurrentPhaseTag = PhaseTag;
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}

	OnMatchPhaseChanged(PhaseTag);
	BroadcastMatchState();
}

void UOceanAdventureNavalMatchComponent::CollectLivingTeams(TArray<int32>& OutTeams) const
{
	OutTeams.Reset();

	const AGameStateBase* GameState = GetGameState<AGameStateBase>();
	if (!GameState)
	{
		return;
	}

	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (!PlayerState)
		{
			continue;
		}

		const APawn* Pawn = PlayerState->GetPawn();
		const ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(Pawn);
		if (!Pawn || (Health && Health->IsDeadOrDying()))
		{
			continue;
		}

		const int32 TeamId = NavalTeam::GetTeamId(PlayerState);
		if (NavalTeam::IsValidTeam(TeamId))
		{
			OutTeams.AddUnique(TeamId);
		}
	}
}

void UOceanAdventureNavalMatchComponent::EvaluateMatch()
{
	if (bMatchOver || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const float Elapsed = GetElapsedSeconds();

	if (!bBeaconActivated && Elapsed >= BeaconActivationSeconds)
	{
		ActivateBeacon();
	}

	TArray<int32> LivingTeams;
	CollectLivingTeams(LivingTeams);

	// A team can look eliminated for a moment while a player is between pawns, so the
	// condition has to hold before it decides the match.
	if (LivingTeams.Num() <= 1)
	{
		EliminationHeldSeconds += EvaluationInterval;
		if (EliminationHeldSeconds >= EliminationConfirmSeconds)
		{
			FinishMatch(
				LivingTeams.Num() == 1 ? ENavalMatchOutcome::Elimination : ENavalMatchOutcome::Draw,
				LivingTeams.Num() == 1 ? LivingTeams[0] : INDEX_NONE);
			return;
		}
	}
	else
	{
		EliminationHeldSeconds = 0.0f;
	}

	if (Elapsed >= HardTimeLimitSeconds)
	{
		HandleTimeLimitReached();
		return;
	}

	BroadcastMatchState();
}

void UOceanAdventureNavalMatchComponent::ActivateBeacon()
{
	bBeaconActivated = true;
	SetMatchPhase(OceanAdventureNavalTags::Phase_NavalP0_SuddenDeath);

	if (AOceanAdventureNavalBeaconActor* BeaconActor = ResolveBeacon())
	{
		BeaconActor->SetBeaconActive(true);
	}
	else
	{
		// Without a beacon the match still ends on the clock, but the sudden-death pressure
		// the P0 script depends on is missing, so this is worth saying out loud.
		UE_LOG(
			LogOceanAdventure,
			Warning,
			TEXT("[NavalMatch] Sudden death started with no beacon in the level"));
	}

	BroadcastMatchState();
}

void UOceanAdventureNavalMatchComponent::HandleBeaconCaptured(
	AOceanAdventureNavalBeaconActor* /*CapturedBeacon*/, int32 TeamId)
{
	FinishMatch(ENavalMatchOutcome::BeaconCapture, TeamId);
}

void UOceanAdventureNavalMatchComponent::HandleTimeLimitReached()
{
	const int32 Winner = ResolveTimeoutWinner();
	FinishMatch(
		Winner != INDEX_NONE ? ENavalMatchOutcome::TimeoutJudgement : ENavalMatchOutcome::Draw,
		Winner);
}

int32 UOceanAdventureNavalMatchComponent::ResolveTimeoutWinner() const
{
	// Survivors first, then how close anyone came to the beacon, then how much hull is left:
	// the same order a player would use to say who was winning.
	TMap<int32, int32> SurvivorsByTeam;
	if (const AGameStateBase* GameState = GetGameState<AGameStateBase>())
	{
		for (const APlayerState* PlayerState : GameState->PlayerArray)
		{
			const APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;
			const ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(Pawn);
			if (!Pawn || (Health && Health->IsDeadOrDying()))
			{
				continue;
			}

			const int32 TeamId = NavalTeam::GetTeamId(PlayerState);
			if (NavalTeam::IsValidTeam(TeamId))
			{
				SurvivorsByTeam.FindOrAdd(TeamId)++;
			}
		}
	}

	TMap<int32, float> HullByTeam;
	if (const UNavalRegistrySubsystem* Registry = UNavalRegistrySubsystem::Get(this))
	{
		for (const TWeakObjectPtr<UNavalVesselComponent>& Entry : Registry->GetVessels())
		{
			const UNavalVesselComponent* Vessel = Entry.Get();
			if (Vessel && NavalTeam::IsValidTeam(Vessel->GetTeamId()))
			{
				HullByTeam.FindOrAdd(Vessel->GetTeamId()) += Vessel->GetHullFraction();
			}
		}
	}

	const AOceanAdventureNavalBeaconActor* BeaconActor = Beacon.Get();
	const int32 BeaconTeam = BeaconActor ? BeaconActor->GetCapturingTeamId() : INDEX_NONE;
	const float BeaconProgress = BeaconActor ? BeaconActor->GetCaptureProgress() : 0.0f;

	int32 BestTeam = INDEX_NONE;
	int32 BestSurvivors = -1;
	float BestBeaconProgress = -1.0f;
	float BestHull = -1.0f;

	for (const TPair<int32, int32>& Pair : SurvivorsByTeam)
	{
		const int32 TeamId = Pair.Key;
		const int32 Survivors = Pair.Value;
		const float TeamBeaconProgress = (TeamId == BeaconTeam) ? BeaconProgress : 0.0f;
		const float TeamHull = HullByTeam.FindRef(TeamId);

		const bool bBetter = Survivors > BestSurvivors
			|| (Survivors == BestSurvivors && TeamBeaconProgress > BestBeaconProgress)
			|| (Survivors == BestSurvivors && FMath::IsNearlyEqual(TeamBeaconProgress, BestBeaconProgress)
				&& TeamHull > BestHull);
		if (bBetter)
		{
			BestTeam = TeamId;
			BestSurvivors = Survivors;
			BestBeaconProgress = TeamBeaconProgress;
			BestHull = TeamHull;
		}
	}

	return BestTeam;
}

void UOceanAdventureNavalMatchComponent::FinishMatch(ENavalMatchOutcome NewOutcome, int32 NewWinningTeamId)
{
	if (bMatchOver)
	{
		return;
	}

	bMatchOver = true;
	Outcome = NewOutcome;
	WinningTeamId = NewWinningTeamId;

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EvaluationTimerHandle);
	}
	if (AOceanAdventureNavalBeaconActor* BeaconActor = Beacon.Get())
	{
		BeaconActor->SetBeaconActive(false);
	}

	SetMatchPhase(OceanAdventureNavalTags::Phase_NavalP0_PostGame);
	GetOwner()->ForceNetUpdate();
	BroadcastMatchState();

	UE_LOG(
		LogOceanAdventure,
		Display,
		TEXT("[NavalMatch] Finished outcome=%d winner=%d elapsed=%.1f"),
		static_cast<int32>(Outcome),
		WinningTeamId,
		GetElapsedSeconds());
}

bool UOceanAdventureNavalMatchComponent::IsLifeRaftAvailable(int32 TeamId) const
{
	return NavalTeam::IsValidTeam(TeamId) && !LifeRaftSpentTeams.Contains(TeamId);
}

bool UOceanAdventureNavalMatchComponent::ConsumeLifeRaft(int32 TeamId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsLifeRaftAvailable(TeamId))
	{
		return false;
	}

	LifeRaftSpentTeams.Add(TeamId);
	GetOwner()->ForceNetUpdate();
	return true;
}

void UOceanAdventureNavalMatchComponent::RestartMatch()
{
	UWorld* World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// Reconnect anchors belong to the match that recorded them. Travel replaces the game
	// state and takes the table with it, but clearing here keeps that from being a silent
	// dependency on how the restart happens to be implemented.
	if (AGameStateBase* GameState = GetGameState<AGameStateBase>())
	{
		if (UOceanAdventureNavalReconnectComponent* Reconnect =
				GameState->FindComponentByClass<UOceanAdventureNavalReconnectComponent>())
		{
			Reconnect->ClearAllAnchors();
		}
	}

	// Back into the same map. P0 measures "would you play again", so getting there has to be
	// one action rather than a trip through the front end.
	UE_LOG(LogOceanAdventure, Display, TEXT("[NavalMatch] Restarting match"));
	World->ServerTravel(TEXT("?restart"), /*bAbsolute=*/false);
}

void UOceanAdventureNavalMatchComponent::OnRep_MatchResult()
{
	BroadcastMatchState();
}

void UOceanAdventureNavalMatchComponent::BroadcastMatchState() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FOceanAdventureNavalMatchStateMessage Message;
	Message.ElapsedSeconds = GetElapsedSeconds();
	Message.RemainingSeconds = GetRemainingSeconds();
	Message.Outcome = Outcome;
	Message.WinningTeamId = WinningTeamId;
	Message.bMatchOver = bMatchOver;
	Message.PhaseTag = CurrentPhaseTag;

	UGameplayMessageSubsystem::Get(World).BroadcastMessage(
		OceanAdventureNavalTags::Message_Naval_MatchState, Message);
}

namespace OceanAdventureNavalMatchConsole
{
	/**
	 * Playtest convenience: restarting from the console keeps a session moving when the UI
	 * button is not wired up yet.
	 */
	static FAutoConsoleCommandWithWorld RestartMatchCommand(
		TEXT("NavalP0.RestartMatch"),
		TEXT("Restarts the current naval P0 match on the server."),
		FConsoleCommandWithWorldDelegate::CreateLambda(
			[](UWorld* World)
			{
				const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
				UOceanAdventureNavalMatchComponent* MatchComponent = GameState
					? GameState->FindComponentByClass<UOceanAdventureNavalMatchComponent>()
					: nullptr;
				if (MatchComponent)
				{
					MatchComponent->RestartMatch();
				}
			}));
}
