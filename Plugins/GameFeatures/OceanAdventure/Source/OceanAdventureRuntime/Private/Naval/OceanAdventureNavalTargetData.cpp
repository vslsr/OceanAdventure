// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/OceanAdventureNavalTargetData.h"

#include "Engine/NetSerialization.h"
#include "GameFramework/Actor.h"

bool FOceanAdventureNavalTargetData::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	// The station has to go through the package map to survive the trip; a raw archive write
	// would only move the pointer's bits.
	UObject* StationObject = StationActor.Get();
	const bool bStationSerialized = Map && Map->SerializeObject(Ar, AActor::StaticClass(), StationObject);
	if (Ar.IsLoading())
	{
		StationActor = Cast<AActor>(StationObject);
	}

	uint8 RequestByte = static_cast<uint8>(Request);
	Ar << RequestByte;
	if (Ar.IsLoading())
	{
		Request = static_cast<ENavalStationRequest>(RequestByte);
	}

	bool bAimSuccess = true;
	AimLocation.NetSerialize(Ar, Map, bAimSuccess);
	Ar << QuantizedCharge;

	Ar << QuantizedThrottle;
	Ar << QuantizedSteer;
	Ar << QuantizedWorldMoveX;
	Ar << QuantizedWorldMoveY;
	Ar.SerializeBits(&bHasFacingTarget, 1);

	bOutSuccess = bStationSerialized && bAimSuccess;
	return true;
}
