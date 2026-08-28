// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Abilities/GameplayAbilityTargetTypes.h"

#include "OceanAdventureNavalTargetData.generated.h"

/** What one naval station request is asking for. */
UENUM()
enum class ENavalStationRequest : uint8
{
	/** Take the wheel or step up to the gun. */
	Occupy,
	/** Leave the station. */
	Release,
	/** A throttle/steer, direct-move/facing, or aim sample while occupying. */
	Control,
	/** Pull the trigger on a heavy weapon. */
	Fire,
	/** Commit the emergency hull repair after the channel finished. */
	CommitRepair,
	/** Put down the team's one folding life raft at the carried location. */
	DeployLifeRaft,
	/** Set up a field heavy weapon at the carried location. */
	DeployHeavyWeapon
};

/**
 * One naval station request travelling through GAS' target-data channel.
 *
 * Same contract as the build placement data: it is a request, never an authorisation. The
 * server re-runs CanOccupy / CanFire / CanBeginEmergencyRepair against its own state, so a
 * client that lies about its aim, its distance or who is at the wheel gets nothing.
 */
USTRUCT()
struct FOceanAdventureNavalTargetData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> StationActor;

	UPROPERTY()
	ENavalStationRequest Request = ENavalStationRequest::Occupy;

	/** Heavy-weapon aim, or the DirectPlanar hull-facing target. */
	UPROPERTY()
	FVector_NetQuantize100 AimLocation = FVector::ZeroVector;

	/** 0..100 charge for a heavy-weapon shot. The server clamps and recomputes the arc. */
	UPROPERTY()
	uint8 QuantizedCharge = 100;

	void SetChargeAlpha(float ChargeAlpha)
	{
		QuantizedCharge = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(ChargeAlpha, 0.0f, 1.0f) * 100.0f));
	}

	float GetChargeAlpha() const { return QuantizedCharge / 100.0f; }

	/** -1..1, quantised to a byte; a boat's throttle does not need more precision than that. */
	UPROPERTY()
	int8 QuantizedThrottle = 0;

	UPROPERTY()
	int8 QuantizedSteer = 0;

	void SetControlIntent(float Throttle, float Steer)
	{
		QuantizedThrottle = static_cast<int8>(FMath::RoundToInt(FMath::Clamp(Throttle, -1.0f, 1.0f) * 100.0f));
		QuantizedSteer = static_cast<int8>(FMath::RoundToInt(FMath::Clamp(Steer, -1.0f, 1.0f) * 100.0f));
		QuantizedWorldMoveX = 0;
		QuantizedWorldMoveY = 0;
		bHasFacingTarget = false;
	}

	float GetThrottle() const { return QuantizedThrottle / 100.0f; }
	float GetSteer() const { return QuantizedSteer / 100.0f; }

	/** World-space XY movement for DirectPlanar vessels, quantised independently of helm axes. */
	UPROPERTY()
	int8 QuantizedWorldMoveX = 0;

	UPROPERTY()
	int8 QuantizedWorldMoveY = 0;

	UPROPERTY()
	bool bHasFacingTarget = false;

	void SetDirectControlIntent(
		const FVector2D& WorldMoveIntent,
		const FVector& FacingTarget,
		bool bInHasFacingTarget)
	{
		const FVector2D Clamped = WorldMoveIntent.GetClampedToMaxSize(1.0f);
		QuantizedThrottle = 0;
		QuantizedSteer = 0;
		QuantizedWorldMoveX = static_cast<int8>(
			FMath::RoundToInt(FMath::Clamp(Clamped.X, -1.0f, 1.0f) * 100.0f));
		QuantizedWorldMoveY = static_cast<int8>(
			FMath::RoundToInt(FMath::Clamp(Clamped.Y, -1.0f, 1.0f) * 100.0f));
		AimLocation = FacingTarget;
		bHasFacingTarget = bInHasFacingTarget;
	}

	FVector2D GetWorldMoveIntent() const
	{
		return FVector2D(QuantizedWorldMoveX / 100.0f, QuantizedWorldMoveY / 100.0f);
	}

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FOceanAdventureNavalTargetData::StaticStruct();
	}

	virtual FString ToString() const override
	{
		return FString::Printf(
			TEXT("NavalTargetData station=%s request=%d throttle=%.2f steer=%.2f move=(%.2f,%.2f) facing=%d"),
			*GetNameSafe(StationActor.Get()),
			static_cast<int32>(Request),
			GetThrottle(),
			GetSteer(),
			GetWorldMoveIntent().X,
			GetWorldMoveIntent().Y,
			bHasFacingTarget);
	}

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <>
struct TStructOpsTypeTraits<FOceanAdventureNavalTargetData>
	: public TStructOpsTypeTraitsBase2<FOceanAdventureNavalTargetData>
{
	enum
	{
		WithNetSerializer = true
	};
};
