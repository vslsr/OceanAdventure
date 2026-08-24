// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "NavalStationInterface.generated.h"

struct FGameplayTag;

UINTERFACE(MinimalAPI, NotBlueprintable)
class UNavalStationInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * The three things anything can be asked that a player walks up to and operates.
 *
 * The heavy weapon and the helm answer these identically in shape but from different places:
 * the gun is its own Actor and owns its seat, while the wheel is a console Actor whose state
 * lives on the vessel's UNavalHelmComponent. Without this, every caller that wants "the
 * nearest thing I could use" has to switch on the concrete type and reach for a differently
 * named accept function -- which is how the two station kinds drifted apart in the first place.
 *
 * Deliberately not Blueprintable: implementing a station means owning replicated occupancy and
 * a server-authoritative accept check, which is C++ work.
 */
class NAVALCORERUNTIME_API INavalStationInterface
{
	GENERATED_BODY()

public:
	/**
	 * Where the seat actually is, which is not always the actor's origin -- the helm console
	 * sits well forward of the vessel it belongs to. Range checks measure to this.
	 */
	virtual FVector GetStationWorldLocation() const = 0;

	/** How close an operator has to stand. The same range the accept check applies. */
	virtual double GetStationInteractionRange() const = 0;

	/**
	 * The station's own accept check: functional, right team, seat free, close enough.
	 *
	 * This is the authority on whether an operator may take the station, so a caller that is
	 * only deciding what to *show* should route through it too rather than re-deriving a
	 * cheaper approximation that can disagree with what the server will do.
	 */
	virtual bool CanOperateStation(const AActor* Candidate, FGameplayTag& OutFailReason) const = 0;
};
