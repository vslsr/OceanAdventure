// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Framework tags for carrying: what kind of thing this is, and every reason a carry can be
 * refused. Input tags, status tags and message channels belong to the gameplay layer, which
 * is the only place that knows GAS exists.
 */
namespace CarryGameplayTags
{
	/**
	 * A large object held in both hands. It never enters an inventory: design 7.10 wants the
	 * carrier slowed, exposed and droppable, and a backpack removes all three at once.
	 */
	CARRYCORERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Carry_Class_Haulable);

	CARRYCORERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_Carry_NoTarget);
	CARRYCORERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_Carry_TooFar);
	/** Somebody else already has it. */
	CARRYCORERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_Carry_Occupied);
	/** The carrier's hands are full. */
	CARRYCORERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_Carry_HandsFull);
	/** Nothing is being carried, so there is nothing to put down. */
	CARRYCORERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_Carry_NotCarrying);
	/** The carryable or its carrier went away mid-request. */
	CARRYCORERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_Carry_Invalid);
}
