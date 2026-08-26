// Copyright Epic Games, Inc. All Rights Reserved.

#include "Carry/OceanAdventureCarryTargetData.h"

#include "GameFramework/Actor.h"

bool FOceanAdventureCarryTargetData::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	// The target has to go through the package map to survive the trip; a raw archive write
	// would only move the pointer's bits.
	UObject* TargetObject = CarryTarget.Get();
	const bool bTargetSerialized = Map && Map->SerializeObject(Ar, AActor::StaticClass(), TargetObject);
	if (Ar.IsLoading())
	{
		CarryTarget = Cast<AActor>(TargetObject);
	}

	uint8 RequestByte = static_cast<uint8>(Request);
	Ar << RequestByte;
	if (Ar.IsLoading())
	{
		Request = static_cast<EOceanAdventureCarryRequest>(RequestByte);
	}

	bOutSuccess = bTargetSerialized;
	return true;
}
