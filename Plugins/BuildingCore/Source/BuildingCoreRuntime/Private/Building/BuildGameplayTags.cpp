// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/BuildGameplayTags.h"

namespace BuildGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(Fail_BadDefinition, "Build.Fail.BadDefinition");
	UE_DEFINE_GAMEPLAY_TAG(Fail_Occupied, "Build.Fail.Occupied");
	UE_DEFINE_GAMEPLAY_TAG(Fail_NoSupport, "Build.Fail.NoSupport");
	UE_DEFINE_GAMEPLAY_TAG(Fail_NoResource, "Build.Fail.NoResource");
	UE_DEFINE_GAMEPLAY_TAG(Fail_Blocked, "Build.Fail.Blocked");
	UE_DEFINE_GAMEPLAY_TAG(Fail_LimitReached, "Build.Fail.LimitReached");
	UE_DEFINE_GAMEPLAY_TAG(Fail_NotFound, "Build.Fail.NotFound");
	UE_DEFINE_GAMEPLAY_TAG(Fail_WouldOrphan, "Build.Fail.WouldOrphan");
	UE_DEFINE_GAMEPLAY_TAG(Fail_TooFar, "Build.Fail.TooFar");
}
