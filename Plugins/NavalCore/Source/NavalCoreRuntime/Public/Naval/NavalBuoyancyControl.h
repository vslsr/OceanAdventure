// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "NavalBuoyancyControl.generated.h"

UINTERFACE(MinimalAPI, NotBlueprintable)
class UNavalBuoyancyControl : public UInterface
{
	GENERATED_BODY()
};

/**
 * Lets the vessel state machine stop a host floating without knowing how it floats.
 *
 * A wreck has to stop being held at the waterline, but buoyancy belongs to whichever host
 * implements it -- a raft samples the ocean surface, a future hull might use real physics.
 * The framework only needs the on/off switch.
 */
class NAVALCORERUNTIME_API INavalBuoyancyControl
{
	GENERATED_BODY()

public:
	virtual void SetBuoyancyEnabled(bool bEnabled) = 0;
};
