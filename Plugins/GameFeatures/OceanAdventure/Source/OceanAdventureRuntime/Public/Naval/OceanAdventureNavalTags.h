// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Gameplay-layer tags for the naval P0 loop.
 *
 * NavalCore owns the framework's own tags (part states, refusal reasons, message channels).
 * What lives here is everything that only means something once GAS, Enhanced Input and the
 * match are involved: input actions, ability-blocking status tags and match messages.
 */
namespace OceanAdventureNavalTags
{
	// Input. Bound through ULyraInputConfig, never by reading keys directly.
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Naval_Interact);
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Naval_Fire);
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Naval_Leave);
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Naval_Repair);
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Naval_DeployHeavyWeapon);
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Naval_DeployLifeRaft);

	/**
	 * Present while the player is steering. Design 1.2: this is a state the character enters,
	 * not a different game -- so it blocks the weapon abilities rather than swapping pawns.
	 */
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Naval_Steering);

	/** Present while the player is standing at a heavy weapon. */
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Naval_OperatingHeavyWeapon);

	/** Short commitment after leaving a station before light weapons come back up. */
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Naval_StationExitLock);

	/** Present while the emergency repair is being channelled. */
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Naval_Repairing);

	// Match phases, started through ULyraGamePhaseSubsystem.
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_NavalP0_Warmup);
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_NavalP0_Skirmish);
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_NavalP0_SuddenDeath);
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_NavalP0_PostGame);

	// Match message channels.
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Message_Naval_MatchState);
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Message_Naval_Beacon);
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Message_Naval_StationFailed);

	/** Payload tag on the damage GameplayEffect the projectile relay applies. */
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Naval_Damage);
}
