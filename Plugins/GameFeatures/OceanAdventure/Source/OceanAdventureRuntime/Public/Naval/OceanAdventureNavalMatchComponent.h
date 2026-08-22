// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/GameStateComponent.h"
#include "GameplayTagContainer.h"
#include "Naval/OceanAdventureNavalMessages.h"

#include "OceanAdventureNavalMatchComponent.generated.h"

class AOceanAdventureNavalBeaconActor;

/**
 * The P0 match script, on the game state.
 *
 * Design 1.1 makes "明确结束" a requirement rather than a nicety: a P0 session has to finish
 * on its own so testers answer "would you play again" instead of "when do we stop". So this
 * runs one clock with three ways out -- eliminate the other side, hold the beacon, or let the
 * limit expire and be judged -- and a restart that needs one action.
 *
 * The 0:00 / 5:30 / 8:00 shape comes straight from the P0 script: an opening where nobody can
 * hide behind distance, a beacon that switches on and makes hiding lose, and a hard stop.
 */
UCLASS(BlueprintType, ClassGroup = (OceanAdventure), meta = (BlueprintSpawnableComponent))
class OCEANADVENTURERUNTIME_API UOceanAdventureNavalMatchComponent : public UGameStateComponent
{
	GENERATED_BODY()

public:
	UOceanAdventureNavalMatchComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	float GetElapsedSeconds() const;

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	float GetRemainingSeconds() const;

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	ENavalMatchOutcome GetOutcome() const { return Outcome; }

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	int32 GetWinningTeamId() const { return WinningTeamId; }

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	bool IsMatchOver() const { return bMatchOver; }

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	FGameplayTag GetCurrentPhaseTag() const { return CurrentPhaseTag; }

	/**
	 * 折叠救生筏: once per team per match, and only while that team has no working ship.
	 *
	 * The entitlement lives on the game state rather than on a player, because design 8.8
	 * shares it across the whole team and it has to survive whichever member dies.
	 */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval")
	bool IsLifeRaftAvailable(int32 TeamId) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "OceanAdventure|Naval")
	bool ConsumeLifeRaft(int32 TeamId);

	/** 一键重开: travels back into the same map so the next test starts immediately. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "OceanAdventure|Naval")
	void RestartMatch();

protected:
	/** 5:30 in the P0 script: the beacon switches on and hiding stops working. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "0.0", Units = "s"))
	float BeaconActivationSeconds = 330.0f;

	/** 8:00: the hard stop that guarantees a result. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "10.0", Units = "s"))
	float HardTimeLimitSeconds = 480.0f;

	/** How long "only one team still standing" has to hold before it counts as a win. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "0.0", Units = "s"))
	float EliminationConfirmSeconds = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "0.1", Units = "s"))
	float EvaluationInterval = 1.0f;

	/**
	 * Seam for Lyra's game phases.
	 *
	 * The rules above are the authority on the match either way; this is where a Blueprint
	 * subclass calls Start Phase so UI, music and spawning can react to the same script
	 * through the system Lyra already has for it.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "OceanAdventure|Naval")
	void OnMatchPhaseChanged(FGameplayTag PhaseTag);

	UPROPERTY(Replicated)
	double MatchStartServerTime = 0.0;

	UPROPERTY(ReplicatedUsing = OnRep_MatchResult)
	FGameplayTag CurrentPhaseTag;

	UPROPERTY(ReplicatedUsing = OnRep_MatchResult)
	ENavalMatchOutcome Outcome = ENavalMatchOutcome::Undecided;

	UPROPERTY(ReplicatedUsing = OnRep_MatchResult)
	int32 WinningTeamId = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_MatchResult)
	bool bMatchOver = false;

	/** Teams that have already spent their one life raft. Replicated so the HUD can grey it out. */
	UPROPERTY(Replicated)
	TArray<int32> LifeRaftSpentTeams;

	UFUNCTION()
	void OnRep_MatchResult();

private:
	void SetMatchPhase(FGameplayTag PhaseTag);
	void EvaluateMatch();
	void ActivateBeacon();
	void HandleBeaconCaptured(AOceanAdventureNavalBeaconActor* Beacon, int32 TeamId);
	void HandleTimeLimitReached();
	void FinishMatch(ENavalMatchOutcome NewOutcome, int32 NewWinningTeamId);
	void BroadcastMatchState() const;

	/** Living characters per team, resolved from the player states rather than the world. */
	void CollectLivingTeams(TArray<int32>& OutTeams) const;

	/** Tiebreak when the clock runs out: survivors, then capture progress, then hull left. */
	int32 ResolveTimeoutWinner() const;

	AOceanAdventureNavalBeaconActor* ResolveBeacon();

	TWeakObjectPtr<AOceanAdventureNavalBeaconActor> Beacon;
	FDelegateHandle BeaconCapturedHandle;
	FTimerHandle EvaluationTimerHandle;
	float EliminationHeldSeconds = 0.0f;
	bool bBeaconActivated = false;
};
