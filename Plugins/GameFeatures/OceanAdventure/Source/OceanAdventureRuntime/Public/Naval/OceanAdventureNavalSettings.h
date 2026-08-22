// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"

#include "OceanAdventureNavalSettings.generated.h"

class AActor;
class UGameplayEffect;

/**
 * Project settings for the parts of the naval loop that have to reach GAS.
 *
 * NavalCore settles structure and hull damage itself, but damage to characters belongs to
 * Lyra's attribute set, so the relay needs to be told which GameplayEffect to apply. Keeping
 * that here rather than hard-coding a class keeps the framework free of a Lyra reference and
 * lets the effect be swapped per project.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Ocean Adventure Naval"))
class OCEANADVENTURERUNTIME_API UOceanAdventureNavalSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UOceanAdventureNavalSettings();

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }

	static const UOceanAdventureNavalSettings& Get();

	/** Applied to a character hit by a naval projectile, with the damage as a SetByCaller. */
	UPROPERTY(Config, EditAnywhere, Category = "Naval|Damage")
	TSoftClassPtr<UGameplayEffect> ProjectileDamageEffect;

	/** SetByCaller key the damage magnitude is written under. */
	UPROPERTY(Config, EditAnywhere, Category = "Naval|Damage")
	FGameplayTag DamageSetByCallerTag;

	/**
	 * Light weapons against naval structure, as a fraction of their character damage.
	 * Design 7.10 asks for 20%-35%: a fallback way to break something, never a siege tool.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Naval|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LightWeaponStructureDamageScale = 0.28f;

	/** Light weapons against a hull. Design 7.10 asks for 10%-25%. */
	UPROPERTY(Config, EditAnywhere, Category = "Naval|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LightWeaponHullDamageScale = 0.18f;

	/** Baseline damage assumed for a light weapon hit when the weapon carries no value. */
	UPROPERTY(Config, EditAnywhere, Category = "Naval|Damage", meta = (ClampMin = "0.0"))
	float LightWeaponBaseDamage = 25.0f;

	/**
	 * The 折叠救生筏 hull spawned after a team loses its ship.
	 *
	 * A soft path in project config rather than an asset reference inside either feature:
	 * the raft lives in the Raft feature and the ability that spawns it lives in the gameplay
	 * feature, and those two may not reference each other's content.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Naval|LifeRaft", meta = (MetaClass = "/Script/Engine.Actor"))
	TSoftClassPtr<AActor> LifeRaftClass;

	/** Design 8.8: 25%-35% of a starting ship's hull. */
	UPROPERTY(Config, EditAnywhere, Category = "Naval|LifeRaft", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float LifeRaftHullScale = 0.3f;

	/** Design 8.8: 65%-75% of a starting ship's speed. Enough to relocate, not to hunt. */
	UPROPERTY(Config, EditAnywhere, Category = "Naval|LifeRaft", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float LifeRaftSpeedScale = 0.7f;

	/** Design 8.8: 4-6 seconds to deploy, interrupted by damage. */
	UPROPERTY(Config, EditAnywhere, Category = "Naval|LifeRaft", meta = (ClampMin = "0.0", Units = "s"))
	float LifeRaftDeploySeconds = 5.0f;

	/** The field emplacement spawned by the heavy-weapon deploy ability. */
	UPROPERTY(Config, EditAnywhere, Category = "Naval|HeavyWeapon", meta = (MetaClass = "/Script/NavalCoreRuntime.NavalHeavyWeaponActor"))
	TSoftClassPtr<AActor> GroundHeavyWeaponClass;

	/** Ground the legs have to span. Design 7.10 caps the slope at about 10-12 degrees. */
	UPROPERTY(Config, EditAnywhere, Category = "Naval|HeavyWeapon", meta = (ClampMin = "50.0", Units = "cm"))
	float GroundHeavyWeaponFootprintRadius = 120.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Naval|HeavyWeapon", meta = (ClampMin = "1.0", ClampMax = "45.0", Units = "deg"))
	float GroundHeavyWeaponMaxSlopeDegrees = 11.0f;

	/** Team-level cap on simultaneous field guns, per design 7.10's world-entity budget. */
	UPROPERTY(Config, EditAnywhere, Category = "Naval|HeavyWeapon", meta = (ClampMin = "1"))
	int32 MaxGroundHeavyWeaponsPerTeam = 2;

	/** How far from a shooter to look for a friendly window that would pass their shot. */
	UPROPERTY(Config, EditAnywhere, Category = "Naval|Ballistics", meta = (ClampMin = "50.0", Units = "cm"))
	float FriendlyWindowSearchRadius = 450.0f;
};
