// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "NavalCoreTypes.generated.h"

/** How a vessel turns player intent into planar motion. Selected by the host's data. */
UENUM(BlueprintType)
enum class ENavalMovementModel : uint8
{
	/** Throttle pushes along the bow and steering applies speed-dependent yaw torque. */
	Helm,
	/** World-space WASD movement and mouse-facing yaw are independent. */
	DirectPlanar
};

/**
 * What a damageable naval part is, for rules that care about the kind rather than the asset.
 *
 * The design keeps these deliberately separate: a hull at zero starts the founder countdown,
 * a helm core at zero only removes active control, and a pontoon at zero collapses buoyancy
 * capacity. One shared health bar would erase all three decisions.
 */
UENUM(BlueprintType)
enum class ENavalPartType : uint8
{
	/** Deck, floor and connecting structure. Carries tonnage, no special rule. */
	Structure,
	/** 船壳. Zero starts the founder countdown, never instant deletion. */
	Hull,
	/** 推进件. Contributes thrust P. */
	Propulsion,
	/** 舵叶. Decides turn efficiency; zero leaves only weak emergency correction. */
	Rudder,
	/** 加固舵芯座 -- the real damage body under the visual helm. Zero disables control only. */
	HelmCore,
	/** 浮筒. Contributes buoyancy capacity C; losing one can force dangerous draft. */
	Pontoon,
	/** 普通墙. Blocks both sides symmetrically. */
	Wall,
	/** 单向射击窗. The only exception to the wall rule. */
	FireWindow,
	/** 重型武器 body. */
	HeavyWeapon
};

/** Lifetime of one part, including the construction window the design requires. */
UENUM(BlueprintType)
enum class ENavalPartState : uint8
{
	/** 部署前摇: no collision, no ballistic blocking, no function, fully refundable. */
	Deploying,
	/**
	 * 实体施工: collision and wall blocking are live, durability climbs from the construction
	 * fraction, but the part provides no function yet.
	 */
	UnderConstruction,
	/** 完成启用: full durability and function. */
	Operational,
	/** Durability at zero but the part still exists as a target and an obstacle. */
	Disabled,
	/** Removed from the world. */
	Destroyed
};

/** 船只状态机 from design 8.5/8.7. Losing the ship never deletes the crew's options. */
UENUM(BlueprintType)
enum class ENavalVesselState : uint8
{
	Operational,
	/** 船壳归零: 15-25s founder countdown, low-speed drift, one emergency repair window. */
	Foundering,
	/** 残骸: loot and rescue only. */
	Wreck
};

/** 载重率 bands from design 7.11. */
UENUM(BlueprintType)
enum class ENavalLoadBand : uint8
{
	/** 0%-70% */
	Light,
	/** 70%-100% */
	Normal,
	/** 100%-115% */
	Overloaded,
	/** >115%: only removal, pontoons and repairs are allowed. */
	SeverelyOverloaded
};

/** 推重比 bands from design 7.11. */
UENUM(BlueprintType)
enum class ENavalThrustBand : uint8
{
	/** <0.65 */
	Sluggish,
	/** 0.65-0.90 */
	Heavy,
	/** 0.90-1.15 */
	Balanced,
	/** >1.15 */
	Nimble
};

/** 舵芯总成 states from design 8.3.1. */
UENUM(BlueprintType)
enum class ENavalHelmState : uint8
{
	Normal,
	/** Core seat damaged: steering and acceleration degrade, with sparks and an alarm. */
	Damaged,
	/** Core at zero: no active throttle or steering, inertia then low-speed drift. */
	CoreDisabled,
	/** An enemy is running the 改旗 progress: anchored or low-speed drift only. */
	Contested
};

/** Why a shot did not reach its target. Drives the muzzle-block line and the shot log. */
UENUM(BlueprintType)
enum class ENavalShotBlockReason : uint8
{
	/** Nothing blocked it. */
	None,
	/** 普通墙 -- symmetric, applies to the shooter's own walls too. */
	Wall,
	/** A window that exists but refuses this shot (wrong team, wrong side, outside the arc). */
	WindowRejected,
	/** Any other blocking geometry, e.g. terrain or a hull. */
	Geometry,
	/** Inside a heavy weapon's minimum engagement range. */
	MinimumRange
};

/**
 * Discrete carry model from design 7.11: tonnage, buoyancy capacity and thrust.
 *
 * Deliberately not a physical mass/centre-of-mass simulation. The three numbers exist so
 * "add another cannon" becomes a readable trade rather than a free upgrade, and so an
 * attacker can pick a different dismantling route: pontoons, thrusters or the guns.
 */
USTRUCT(BlueprintType)
struct NAVALCORERUNTIME_API FNavalLoadState
{
	GENERATED_BODY()

	/** Total tonnage W. */
	UPROPERTY(BlueprintReadOnly, Category = "Naval|Load")
	float Tonnage = 0.0f;

	/** Buoyancy capacity C from the hull core and any working pontoons. */
	UPROPERTY(BlueprintReadOnly, Category = "Naval|Load")
	float BuoyancyCapacity = 1.0f;

	/** Thrust P from working propulsion modules. */
	UPROPERTY(BlueprintReadOnly, Category = "Naval|Load")
	float Thrust = 0.0f;

	/** W / C. */
	UPROPERTY(BlueprintReadOnly, Category = "Naval|Load")
	float LoadRatio = 0.0f;

	/** P / W. */
	UPROPERTY(BlueprintReadOnly, Category = "Naval|Load")
	float ThrustRatio = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Naval|Load")
	ENavalLoadBand LoadBand = ENavalLoadBand::Light;

	UPROPERTY(BlueprintReadOnly, Category = "Naval|Load")
	ENavalThrustBand ThrustBand = ENavalThrustBand::Balanced;

	/**
	 * Set while capacity collapsed below the structure the crew already built -- the
	 * 10-15s window in which they can repair, dump weight or fight back before the hull
	 * starts taking water damage.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Naval|Load")
	bool bDangerousDraft = false;

	bool operator==(const FNavalLoadState& Other) const
	{
		return FMath::IsNearlyEqual(Tonnage, Other.Tonnage)
			&& FMath::IsNearlyEqual(BuoyancyCapacity, Other.BuoyancyCapacity)
			&& FMath::IsNearlyEqual(Thrust, Other.Thrust)
			&& LoadBand == Other.LoadBand
			&& ThrustBand == Other.ThrustBand
			&& bDangerousDraft == Other.bDangerousDraft;
	}

	bool operator!=(const FNavalLoadState& Other) const { return !(*this == Other); }
};

/** Multipliers a load or thrust band applies to handling. 1.0 is the tuning baseline. */
USTRUCT(BlueprintType)
struct NAVALCORERUNTIME_API FNavalHandlingScalars
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Naval|Movement")
	float TopSpeed = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Naval|Movement")
	float Acceleration = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Naval|Movement")
	float Turn = 1.0f;

	/** Continuous mass response. Lower values preserve linear momentum for longer. */
	UPROPERTY(BlueprintReadOnly, Category = "Naval|Movement")
	float LinearResponse = 1.0f;

	/** Continuous mass response for yaw acceleration. */
	UPROPERTY(BlueprintReadOnly, Category = "Naval|Movement")
	float AngularResponse = 1.0f;

	FNavalHandlingScalars& CombineWith(const FNavalHandlingScalars& Other)
	{
		TopSpeed *= Other.TopSpeed;
		Acceleration *= Other.Acceleration;
		Turn *= Other.Turn;
		LinearResponse *= Other.LinearResponse;
		AngularResponse *= Other.AngularResponse;
		return *this;
	}
};

namespace NavalLoad
{
	/** Thresholds live here so the build gate, the HUD and movement never disagree. */
	inline constexpr float NormalLoadThreshold = 0.70f;
	inline constexpr float OverloadThreshold = 1.00f;
	inline constexpr float SevereOverloadThreshold = 1.15f;

	inline constexpr float SluggishThrustThreshold = 0.65f;
	inline constexpr float HeavyThrustThreshold = 0.90f;
	inline constexpr float NimbleThrustThreshold = 1.15f;

	inline ENavalLoadBand BandForLoadRatio(float LoadRatio)
	{
		if (LoadRatio > SevereOverloadThreshold)
		{
			return ENavalLoadBand::SeverelyOverloaded;
		}
		if (LoadRatio > OverloadThreshold)
		{
			return ENavalLoadBand::Overloaded;
		}
		return LoadRatio > NormalLoadThreshold ? ENavalLoadBand::Normal : ENavalLoadBand::Light;
	}

	inline ENavalThrustBand BandForThrustRatio(float ThrustRatio)
	{
		if (ThrustRatio < SluggishThrustThreshold)
		{
			return ENavalThrustBand::Sluggish;
		}
		if (ThrustRatio < HeavyThrustThreshold)
		{
			return ENavalThrustBand::Heavy;
		}
		return ThrustRatio <= NimbleThrustThreshold ? ENavalThrustBand::Balanced : ENavalThrustBand::Nimble;
	}

	inline FNavalHandlingScalars ScalarsForLoadBand(ENavalLoadBand Band)
	{
		FNavalHandlingScalars Scalars;
		switch (Band)
		{
		case ENavalLoadBand::Overloaded:
			// Design 7.11: -15% top speed, -20% turn and acceleration.
			Scalars.TopSpeed = 0.85f;
			Scalars.Acceleration = 0.80f;
			Scalars.Turn = 0.80f;
			break;
		case ENavalLoadBand::SeverelyOverloaded:
			// Placing more tonnage is already blocked; what is left is a boat that wallows.
			Scalars.TopSpeed = 0.70f;
			Scalars.Acceleration = 0.65f;
			Scalars.Turn = 0.65f;
			break;
		default:
			break;
		}
		return Scalars;
	}

	inline FNavalHandlingScalars ScalarsForThrustBand(ENavalThrustBand Band)
	{
		FNavalHandlingScalars Scalars;
		switch (Band)
		{
		case ENavalThrustBand::Sluggish:
			Scalars.TopSpeed = 0.80f;
			Scalars.Acceleration = 0.70f;
			Scalars.Turn = 0.80f;
			break;
		case ENavalThrustBand::Heavy:
			Scalars.Acceleration = 0.875f;
			Scalars.Turn = 0.875f;
			break;
		case ENavalThrustBand::Nimble:
			// Top speed gain is capped so a bare thrust raft cannot outrun everything to the
			// closing circle; the reward is acceleration and turning.
			Scalars.TopSpeed = 1.10f;
			Scalars.Acceleration = 1.25f;
			Scalars.Turn = 1.20f;
			break;
		default:
			break;
		}
		return Scalars;
	}
}
