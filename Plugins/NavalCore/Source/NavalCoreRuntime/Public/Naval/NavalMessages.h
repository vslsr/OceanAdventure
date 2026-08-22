// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "Naval/NavalCoreTypes.h"

#include "NavalMessages.generated.h"

class AActor;
class UNavalPartComponent;

/** Hull state machine transitions, including the founder countdown the HUD counts down. */
USTRUCT(BlueprintType)
struct NAVALCORERUNTIME_API FNavalVesselStateMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> Vessel = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	ENavalVesselState State = ENavalVesselState::Operational;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	float HullFraction = 1.0f;

	/** Seconds left before the vessel becomes a wreck; 0 outside the foundering state. */
	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	float FounderSecondsRemaining = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	int32 TeamId = INDEX_NONE;

	/** True while an emergency repair is still available to the owning team. */
	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	bool bEmergencyRepairAvailable = true;
};

USTRUCT(BlueprintType)
struct NAVALCORERUNTIME_API FNavalLoadStateMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> Vessel = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	FNavalLoadState LoadState;
};

USTRUCT(BlueprintType)
struct NAVALCORERUNTIME_API FNavalPartStateMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> Vessel = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> PartActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	ENavalPartType PartType = ENavalPartType::Structure;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	ENavalPartState PartState = ENavalPartState::Operational;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	float DurabilityFraction = 1.0f;
};

USTRUCT(BlueprintType)
struct NAVALCORERUNTIME_API FNavalHelmMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> Vessel = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	ENavalHelmState HelmState = ENavalHelmState::Normal;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> Operator = nullptr;

	/** 0..1 progress of an enemy 改旗 attempt. */
	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	float CaptureProgress = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	int32 CapturingTeamId = INDEX_NONE;
};

/**
 * Why a shot stopped short. The design forbids "silent" wall hits: a shot eaten by the
 * shooter's own wall must be visible to the shooter, or players never learn the rule.
 */
USTRUCT(BlueprintType)
struct NAVALCORERUNTIME_API FNavalShotBlockedMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> Instigator = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> Blocker = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	ENavalShotBlockReason Reason = ENavalShotBlockReason::None;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	FVector ImpactLocation = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct NAVALCORERUNTIME_API FNavalHeavyWeaponMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> Weapon = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> Operator = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	ENavalPartState WeaponState = ENavalPartState::Operational;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	float DurabilityFraction = 1.0f;

	/** Seconds until the weapon can fire again; drives the reload arc on the HUD. */
	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	float ReloadSecondsRemaining = 0.0f;
};

/**
 * A resolved projectile impact.
 *
 * Structural and hull damage is applied by the framework itself, because save restore and
 * cheats have to reach it without GAS. Damage to characters is left to the gameplay layer:
 * it subscribes to this channel and applies a GameplayEffect, which keeps GAS -- and Lyra --
 * out of NavalCore entirely.
 */
USTRUCT(BlueprintType)
struct NAVALCORERUNTIME_API FNavalProjectileImpactMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> Projectile = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> Instigator = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> HitActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	FVector ImpactLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	FVector ImpactNormal = FVector::UpVector;

	/** Already applied by the framework when the target was structure or hull. */
	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	float StructureDamageApplied = 0.0f;

	/** Not applied yet: the gameplay layer owns damage to characters. */
	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	float PendingCharacterDamage = 0.0f;

	/** Suppression radius around the impact; zero for a pure point hit. */
	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	float SuppressionRadius = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	ENavalShotBlockReason BlockReason = ENavalShotBlockReason::None;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	int32 InstigatorTeamId = INDEX_NONE;
};

/** Directional warnings: boarding, contest, dangerous draft, an enemy setting up a gun. */
USTRUCT(BlueprintType)
struct NAVALCORERUNTIME_API FNavalAlertMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	TObjectPtr<AActor> Vessel = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	FGameplayTag AlertTag;

	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	FVector WorldLocation = FVector::ZeroVector;

	/** Which team should react to it; INDEX_NONE means everyone nearby. */
	UPROPERTY(BlueprintReadWrite, Category = "Naval")
	int32 TeamId = INDEX_NONE;
};
