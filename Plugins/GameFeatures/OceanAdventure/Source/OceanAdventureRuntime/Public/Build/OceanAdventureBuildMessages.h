// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"

#include "OceanAdventureBuildMessages.generated.h"

/** Broadcast on OceanAdventureBuildTags::Message_Build_Failed for UI, audio and telemetry. */
USTRUCT(BlueprintType)
struct FOceanAdventureBuildFailedMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Building")
	FGameplayTag FailReason;

	UPROPERTY(BlueprintReadWrite, Category = "Building")
	TObjectPtr<AActor> HostActor = nullptr;
};
