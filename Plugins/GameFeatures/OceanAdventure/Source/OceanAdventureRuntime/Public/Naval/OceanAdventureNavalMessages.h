// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"

#include "OceanAdventureNavalMessages.generated.h"

class AActor;

/** How a P0 match ended, for the result screen and for the playtest log. */
UENUM(BlueprintType)
enum class ENavalMatchOutcome : uint8
{
	/** Still running. */
	Undecided,
	/** Every other team was eliminated. */
	Elimination,
	/** The central beacon was held for the full capture. */
	BeaconCapture,
	/** The clock ran out and the tiebreak decided it. */
	TimeoutJudgement,
	/** No team met any condition -- recorded rather than silently ignored. */
	Draw
};

/** Broadcast on OceanAdventureNavalTags::Message_Naval_StationFailed. */
USTRUCT(BlueprintType)
struct FOceanAdventureNavalStationFailedMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	FGameplayTag FailReason;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	TObjectPtr<AActor> StationActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	TObjectPtr<AActor> Instigator = nullptr;
};

/**
 * Broadcast on OceanAdventureNavalTags::Message_Naval_StationPrompt when the station the local
 * player could take changes -- including to none.
 *
 * Local only: the proximity component that sends this runs on the owning client, so the HUD
 * can bind to it directly without any of it crossing the wire.
 */
USTRUCT(BlueprintType)
struct FOceanAdventureNavalStationPromptMessage
{
	GENERATED_BODY()

	/** The station now offered, or null when the prompt should be cleared. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	TObjectPtr<AActor> StationActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	bool bAvailable = false;
};

/** Broadcast on OceanAdventureNavalTags::Message_Naval_MatchState whenever the script advances. */
USTRUCT(BlueprintType)
struct FOceanAdventureNavalMatchStateMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	FGameplayTag PhaseTag;

	/** Seconds since the match clock started. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	float ElapsedSeconds = 0.0f;

	/** Seconds until the hard time limit. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	float RemainingSeconds = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	ENavalMatchOutcome Outcome = ENavalMatchOutcome::Undecided;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	int32 WinningTeamId = INDEX_NONE;

	/** True once the match is over and a restart is the only thing left to do. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	bool bMatchOver = false;
};

/** Broadcast on OceanAdventureNavalTags::Message_Naval_Beacon. */
USTRUCT(BlueprintType)
struct FOceanAdventureNavalBeaconMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	TObjectPtr<AActor> Beacon = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	bool bActive = false;

	/** 0..1 towards the capture. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	float CaptureProgress = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	int32 CapturingTeamId = INDEX_NONE;

	/** True while more than one team stands in the ring, which freezes the capture. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Naval")
	bool bContested = false;
};
