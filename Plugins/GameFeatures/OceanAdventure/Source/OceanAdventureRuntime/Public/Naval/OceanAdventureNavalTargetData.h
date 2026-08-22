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
	/** A throttle/steer or aim sample while occupying. */
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

	/** Where a heavy weapon is being pointed. Unused for helm control. */
	UPROPERTY()
	FVector_NetQuantize100 AimLocation = FVector::ZeroVector;

	/** -1..1, quantised to a byte; a boat's throttle does not need more precision than that. */
	UPROPERTY()
	int8 QuantizedThrottle = 0;

	UPROPERTY()
	int8 QuantizedSteer = 0;

	void SetControlIntent(float Throttle, float Steer)
	{
		QuantizedThrottle = static_cast<int8>(FMath::RoundToInt(FMath::Clamp(Throttle, -1.0f, 1.0f) * 100.0f));
		QuantizedSteer = static_cast<int8>(FMath::RoundToInt(FMath::Clamp(Steer, -1.0f, 1.0f) * 100.0f));
	}

	float GetThrottle() const { return QuantizedThrottle / 100.0f; }
	float GetSteer() const { return QuantizedSteer / 100.0f; }

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FOceanAdventureNavalTargetData::StaticStruct();
	}

	virtual FString ToString() const override
	{
		return FString::Printf(
			TEXT("NavalTargetData station=%s request=%d throttle=%.2f steer=%.2f"),
			*GetNameSafe(StationActor.Get()),
			static_cast<int32>(Request),
			GetThrottle(),
			GetSteer());
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
