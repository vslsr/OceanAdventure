// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Weapons/LyraGameplayAbility_RangedWeapon.h"

#include "OceanAdventureGameplayAbility_LightWeapon.generated.h"

/**
 * A Lyra ranged weapon that obeys the project's wall and one-way-window rule.
 *
 * Half of the rule is already true for free: Lyra's weapon trace stops at the first blocking
 * hit, and a naval wall is a blocking Actor, so shots do not pass walls -- including the
 * shooter's own, which is the symmetry the design insists on.
 *
 * What this class adds is the single exception. Before the trace runs it collects the
 * friendly windows this particular shot is authorised to leave through and marks them
 * ignored, so exactly those shots pass and nothing else does. Windows that refuse the shot,
 * and enemy fire coming the other way, stay solid geometry -- the rule is never implemented
 * by switching a window's collision off.
 *
 * It also carries the structural damage light weapons do. Lyra applies its damage through a
 * GameplayEffect on the target's ability system, which a wall does not have, so a Blueprint
 * fire ability derived from this class calls ApplyNavalHitEffects from
 * K2_OnRangedWeaponTargetDataReady to settle hits on naval parts and hulls.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_LightWeapon : public ULyraGameplayAbility_RangedWeapon
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_LightWeapon(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/**
	 * Server-authoritative structural damage for the hits Lyra already resolved.
	 *
	 * Call this from K2_OnRangedWeaponTargetDataReady in the Blueprint fire ability, next to
	 * where it applies its damage effect to pawns.
	 */
	UFUNCTION(BlueprintCallable, Category = "OceanAdventure|Naval")
	void ApplyNavalHitEffects(const FGameplayAbilityTargetDataHandle& TargetData);

protected:
	/** Top-down uses the pawn's cursor-facing actor yaw instead of Lyra's fixed camera pitch. */
	virtual FTransform GetTargetingTransform(
		APawn* SourcePawn,
		ELyraAbilityTargetingSource Source) const override;

	virtual void AddAdditionalTraceIgnoreActors(FCollisionQueryParams& TraceParams) const override;

	/** Damage one light-weapon hit does to a character; scaled down for structure and hulls. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "0.0"))
	float BaseHitDamage = 0.0f;
};
