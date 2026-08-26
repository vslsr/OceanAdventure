// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Abilities/GameplayAbilityTargetTypes.h"

#include "OceanAdventureCarryTargetData.generated.h"

/** What one carry request is asking for. */
UENUM()
enum class EOceanAdventureCarryRequest : uint8
{
	/** Lift the target off the ground. */
	PickUp,
	/** Put down whatever is currently in the carrier's hands. */
	PutDown
};

/**
 * One carry request travelling through GAS' target-data channel.
 *
 * A request, never an authorisation. The server re-runs UCarrierComponent::CanPickUp and the
 * ability's own game rules against its own state, and it picks the put-down location itself,
 * so a client that lies about what it is standing next to gets nothing.
 */
USTRUCT()
struct FOceanAdventureCarryTargetData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> CarryTarget;

	UPROPERTY()
	EOceanAdventureCarryRequest Request = EOceanAdventureCarryRequest::PickUp;

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FOceanAdventureCarryTargetData::StaticStruct();
	}

	virtual FString ToString() const override
	{
		return FString::Printf(
			TEXT("CarryTargetData target=%s request=%d"),
			*GetNameSafe(CarryTarget.Get()),
			static_cast<int32>(Request));
	}

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <>
struct TStructOpsTypeTraits<FOceanAdventureCarryTargetData>
	: public TStructOpsTypeTraitsBase2<FOceanAdventureCarryTargetData>
{
	enum
	{
		WithNetSerializer = true
	};
};
