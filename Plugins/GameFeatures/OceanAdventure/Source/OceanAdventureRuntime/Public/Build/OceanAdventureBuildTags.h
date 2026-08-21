// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

namespace OceanAdventureBuildTags
{
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Build_Mode);
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Build_Confirm);
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Build_Remove);
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Build_Cycle);

	/** Present on the ASC while the build-mode ability is running. */
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Build_Active);

	/** Present while a buildable host is within reach; gates build mode without granting/revoking abilities. */
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Build_HostAvailable);

	/** Channel for FOceanAdventureBuildFailedMessage. */
	OCEANADVENTURERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Message_Build_Failed);
}
