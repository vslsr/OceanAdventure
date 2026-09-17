// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"

#include "OceanAdventureScriptTypes.generated.h"

class AActor;

/**
 * Everything a damage rule is allowed to see about one hit.
 *
 * It is a snapshot rather than a set of live pointers plus "go look it up yourself" on
 * purpose. A rule written in TypeScript runs on the server in the middle of resolving an
 * impact; every extra lookup it makes is a lookup nobody reviewed for cost or for authority.
 * Anything genuinely missing here belongs in this struct, which costs one rebuild -- once.
 */
USTRUCT(BlueprintType)
struct OCEANADVENTURERUNTIME_API FOceanAdventureDamageContext
{
	GENERATED_BODY()

	/** Who fired. May be null for world damage. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	TObjectPtr<AActor> Instigator = nullptr;

	/** What is being hurt. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	TObjectPtr<AActor> Target = nullptr;

	/** The projectile, weapon or volume that carried the damage. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	TObjectPtr<AActor> Causer = nullptr;

	/** What the C++ layer would have applied if no rule were bound. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	float BaseDamage = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FVector ImpactLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FVector ImpactNormal = FVector::UpVector;

	/** Instigator to impact, in centimetres. Negative when there is no instigator. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	float DistanceCm = -1.0f;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	int32 InstigatorTeamId = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	int32 TargetTeamId = INDEX_NONE;

	/** Resolved here rather than left to the rule: both teams have to be valid *and* equal. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	bool bFriendlyFire = false;

	/** Where the damage came from, e.g. a naval projectile or a light weapon. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FGameplayTag SourceTag;

	/** 0..1, or a negative value when the target has no health component. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	float TargetHealthFraction = -1.0f;
};

/** What a damage rule decided. */
USTRUCT(BlueprintType)
struct OCEANADVENTURERUNTIME_API FOceanAdventureDamageVerdict
{
	GENERATED_BODY()

	/** Final damage. Clamped to >= 0 by the caller; a rule cannot heal by returning a negative. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	float Damage = 0.0f;

	/** Drop the hit entirely, cue included. Zero damage instead applies no effect but still cues. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	bool bCancelled = false;

	/** Why, for the HUD and the playtest log: a weak point, a friendly hit, a blocked shot. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FGameplayTag ReasonTag;

	/** Optional cue to play at the impact instead of the default one. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FGameplayTag ImpactCueTag;
};

/** Everything an interaction rule is allowed to see about one attempt. */
USTRUCT(BlueprintType)
struct OCEANADVENTURERUNTIME_API FOceanAdventureInteractionContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	TObjectPtr<AActor> Instigator = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	TObjectPtr<AActor> Target = nullptr;

	/** Which interaction this is: carry, build, repair, a station. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FGameplayTag InteractionTag;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	int32 InstigatorTeamId = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	int32 TargetTeamId = INDEX_NONE;
};

/** What an interaction rule decided. */
USTRUCT(BlueprintType)
struct OCEANADVENTURERUNTIME_API FOceanAdventureInteractionVerdict
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	bool bAllowed = true;

	/**
	 * Why it was refused. A refusal without a reason is the failure mode the carry and build
	 * systems already learned the hard way: the player is told nothing and concludes the game
	 * is broken, so the reason travels on a message and ends up on the HUD.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FGameplayTag DeniedReason;

	/** Optional cue for the success or the refusal. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FGameplayTag CueTag;

	/** Scales the interaction's channel time; 1 leaves it alone. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	float DurationScale = 1.0f;
};

/**
 * The shape every gameplay message takes once it reaches a script.
 *
 * Actors ride as pointers because a script that cannot name the actor it is reacting to can
 * do nothing about it; everything else rides as JSON so a channel can gain a field without a
 * rebuild. That split is the whole design: identity typed, detail loose.
 */
USTRUCT(BlueprintType)
struct OCEANADVENTURERUNTIME_API FOceanAdventureScriptMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FGameplayTag Channel;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	TObjectPtr<AActor> Source = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	TObjectPtr<AActor> Target = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FVector WorldLocation = FVector::ZeroVector;

	/** The one number the channel is mostly about: a fraction, a countdown, a damage value. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	float Magnitude = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FGameplayTag ReasonTag;

	/** The original message, serialised. Empty when the channel had nothing else to say. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Script")
	FString PayloadJson;
};
