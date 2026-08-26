// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"

#include "OceanAdventureCarryMessages.generated.h"

class AActor;

/**
 * Broadcast on OceanAdventureCarryTags::Message_Carry_Failed.
 *
 * A message rather than a client RPC: the HUD, the audio and later the tutorial all want to
 * know that a lift was refused, and none of them should have to be wired into the ability.
 */
USTRUCT(BlueprintType)
struct FOceanAdventureCarryFailedMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Carry")
	FGameplayTag FailReason;

	/** What the player was trying to lift or put down, when there was one. */
	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Carry")
	TObjectPtr<AActor> CarryTarget = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "OceanAdventure|Carry")
	TObjectPtr<AActor> Instigator = nullptr;
};
