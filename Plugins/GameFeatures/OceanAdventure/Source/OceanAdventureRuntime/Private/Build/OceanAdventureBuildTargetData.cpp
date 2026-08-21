// Copyright Epic Games, Inc. All Rights Reserved.

#include "Build/OceanAdventureBuildTargetData.h"

#include "Engine/NetSerialization.h"
#include "GameFramework/Actor.h"

bool FOceanAdventureBuildTargetData::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	// The host actor has to go through the package map to survive the trip; a raw archive
	// write would only move the pointer's bits.
	UObject* HostObject = HostActor.Get();
	bool bHostSerialized = Map && Map->SerializeObject(Ar, AActor::StaticClass(), HostObject);
	if (Ar.IsLoading())
	{
		HostActor = Cast<AActor>(HostObject);
	}

	Ar << SlotKey.Coord.X;
	Ar << SlotKey.Coord.Y;
	Ar << SlotKey.Coord.Level;
	Ar << SlotKey.EdgeIndex;
	Ar << SlotKey.SubCell;

	uint8 SlotByte = static_cast<uint8>(SlotKey.Slot);
	Ar << SlotByte;
	if (Ar.IsLoading())
	{
		SlotKey.Slot = static_cast<EBuildSlotType>(SlotByte);
	}

	Ar << PieceIndex;
	Ar << Rotation;

	bOutSuccess = bHostSerialized;
	return true;
}
