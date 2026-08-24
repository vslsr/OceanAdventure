// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"

#include "NavalRegistrySubsystem.generated.h"

class AActor;
class UNavalVesselComponent;

/**
 * Every vessel and every operable station in the world, kept as lists rather than found by
 * searching.
 *
 * Match rules, the sudden-death beacon and any "nearest ship" query need this set several
 * times a second. Iterating the world for them is the exact pattern the project bans: it
 * scales with the whole level rather than with the handful of ships that exist.
 *
 * Stations are here for the same reason one step later: a proximity prompt has to ask "is
 * there something I could use" a few times a second for every player, and a sphere overlap
 * per player per scan is a scene query bill that buys nothing -- the set of guns and wheels
 * is small, known, and already announces itself at BeginPlay.
 */
UCLASS()
class NAVALCORERUNTIME_API UNavalRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UNavalRegistrySubsystem* Get(const UObject* WorldContextObject);

	void RegisterVessel(UNavalVesselComponent* Vessel);
	void UnregisterVessel(UNavalVesselComponent* Vessel);

	const TArray<TWeakObjectPtr<UNavalVesselComponent>>& GetVessels() const { return Vessels; }

	/** Live vessels only: wrecks are excluded, since nothing should home in on a hulk. */
	UFUNCTION(BlueprintCallable, Category = "Naval|Registry")
	void CollectOperationalVessels(TArray<UNavalVesselComponent*>& OutVessels) const;

	UFUNCTION(BlueprintCallable, Category = "Naval|Registry")
	UNavalVesselComponent* FindNearestVessel(const FVector& Location, int32 TeamFilter = -1) const;

	/** Stations announce themselves at BeginPlay; anything not an INavalStationInterface is ignored. */
	void RegisterStation(AActor* Station);
	void UnregisterStation(AActor* Station);

	/**
	 * Nearest station within an explicit radius, measured to the seat rather than the actor
	 * origin. Ignores whether the station would actually accept anyone -- this is the loose
	 * pre-filter an ability uses before the server has its say.
	 *
	 * Plain C++ rather than a UFUNCTION: both callers are native, and the Blueprint-facing
	 * surface for "what could I use" is the proximity component's own accessor.
	 */
	AActor* FindNearestStation(
		const FVector& Location,
		double Radius,
		TSubclassOf<AActor> StationClass = nullptr) const;

	/**
	 * Nearest station that would accept Candidate right now, by the station's own check.
	 *
	 * RangeScale below 1 tightens the reach without touching the station's configured range,
	 * which is what a prompt wants: it has to stop claiming availability slightly *before*
	 * the real boundary, never after it.
	 */
	AActor* FindReachableStation(
		const AActor* Candidate,
		TSubclassOf<AActor> StationClass = nullptr,
		double RangeScale = 1.0) const;

	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TArray<TWeakObjectPtr<UNavalVesselComponent>> Vessels;
	TArray<TWeakObjectPtr<AActor>> Stations;
};
