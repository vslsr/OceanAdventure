// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Building/BuildGridTypes.h"

#include "OceanAdventureBuildTargetData.generated.h"

/**
 * One placement request travelling through GAS' target-data channel.
 *
 * It is a request, not an authorisation: the server re-runs the full CanPlacePiece check
 * against its own structure state before touching anything.
 */
USTRUCT()
struct FOceanAdventureBuildTargetData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> HostActor;

	UPROPERTY()
	FBuildSlotKey SlotKey;

	UPROPERTY()
	uint16 PieceIndex = 0;

	UPROPERTY()
	uint8 Rotation = 0;

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FOceanAdventureBuildTargetData::StaticStruct();
	}

	virtual FString ToString() const override
	{
		return FString::Printf(
			TEXT("BuildTargetData %s %s piece=%u"),
			*GetNameSafe(HostActor.Get()),
			*SlotKey.Coord.ToString(),
			PieceIndex);
	}

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <>
struct TStructOpsTypeTraits<FOceanAdventureBuildTargetData>
	: public TStructOpsTypeTraitsBase2<FOceanAdventureBuildTargetData>
{
	enum
	{
		WithNetSerializer = true
	};
};
