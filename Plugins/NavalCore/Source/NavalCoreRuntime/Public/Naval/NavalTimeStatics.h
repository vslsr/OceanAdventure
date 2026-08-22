// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UObject;

namespace NavalTime
{
	/**
	 * A clock both sides agree on.
	 *
	 * Countdowns (foundering, capture, reload) are replicated as an end timestamp rather than
	 * a ticking remainder, so a client that joins late or drops a packet still lands on the
	 * same number. That only works against the server clock, which is what GameStateBase
	 * exposes; the fallback keeps standalone and editor worlds working.
	 */
	NAVALCORERUNTIME_API double GetNetworkTimeSeconds(const UObject* WorldContextObject);
}
