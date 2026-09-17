// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Script/OceanAdventureScriptTypes.h"

#include "OceanAdventureCombatScriptLibrary.generated.h"

class AActor;

/**
 * The combat half of the interface TypeScript calls down through.
 *
 * Everything here is deliberately a *verb the game already supports*, not a hole into the
 * engine. Scripts get "apply this much damage to that actor through the configured effect",
 * not an AbilitySystemComponent to drive themselves -- because the second one turns every GAS
 * invariant in this project into something a script can break from a file that never gets
 * reviewed, and because a narrow surface is what makes the generated typings worth trusting.
 *
 * Adding a verb costs one rebuild. Changing what an existing verb decides costs a file save.
 * That is the line this class exists to draw.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureCombatScriptLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Applies damage to a character through the project's configured damage GameplayEffect.
	 *
	 * Server only; a client call is refused and logged. The effect and the SetByCaller key
	 * come from Ocean Adventure Naval settings, so a script never names an asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Script|Combat")
	static bool ApplyCharacterDamage(
		AActor* Target,
		AActor* Instigator,
		AActor* Causer,
		float Damage,
		FVector ImpactLocation);

	/** Team of an actor, walking actor -> controller -> owner. INDEX_NONE when unknown. */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script|Combat")
	static int32 GetTeamId(const AActor* Actor);

	/** True only when both sides resolved to a valid team and those teams differ. */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script|Combat")
	static bool AreEnemies(const AActor* A, const AActor* B);

	/** 0..1 health, or -1 when the actor has no health component. */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script|Combat")
	static float GetHealthFraction(const AActor* Actor);

	/** 0..1 hull of the vessel this actor is part of, or -1 when it is not on one. */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script|Combat")
	static float GetVesselHullFraction(const AActor* Actor);

	/** Centimetres between two actors, or -1 when either is missing. */
	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Script|Combat")
	static float GetDistanceCm(const AActor* A, const AActor* B);

	/**
	 * Pawns within a radius, for splash and suppression rules.
	 *
	 * Capped, and capped on purpose: an unbounded overlap written in a hot-reloaded file is
	 * one typo away from a frame-long query on a busy server, and nobody would connect the
	 * hitch to the rule they edited an hour earlier.
	 */
	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Script|Combat", meta = (WorldContext = "WorldContextObject"))
	static TArray<AActor*> FindPawnsInRadius(
		const UObject* WorldContextObject,
		FVector Center,
		float RadiusCm,
		int32 MaxResults = 32);

	/** Fills in teams, distance, friendly fire and target health for a raw hit. */
	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Script|Combat")
	static FOceanAdventureDamageContext MakeDamageContext(
		AActor* Instigator,
		AActor* Target,
		AActor* Causer,
		float BaseDamage,
		FVector ImpactLocation,
		FVector ImpactNormal,
		FGameplayTag SourceTag);
};
