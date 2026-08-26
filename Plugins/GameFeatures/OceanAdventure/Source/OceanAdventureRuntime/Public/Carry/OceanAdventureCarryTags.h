// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Gameplay-layer tags for carrying.
 *
 * CarryCore owns the framework's tags (what a carryable is, why a carry was refused). What
 * lives here is what only means something once GAS, Enhanced Input and the HUD exist.
 */
namespace OceanAdventureCarryTags
{
	/** Bound to IA_Ocean_Carry through ULyraInputConfig; no key is ever read directly. */
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Carry);

	/**
	 * A gun bolted to a deck or a wall. It comes off through the build system, with the
	 * host's tonnage and firing arcs recomputed; a pair of hands is not that path.
	 */
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_Carry_Mounted);

	/** Refusals, for the HUD and for the playtest log. */
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Message_Carry_Failed);
}
