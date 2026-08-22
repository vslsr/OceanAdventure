// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#include "NavalRegistrySubsystem.generated.h"

class UNavalVesselComponent;

/**
 * Every vessel in the world, kept as a list rather than found by searching.
 *
 * Match rules, the sudden-death beacon and any "nearest ship" query need this set several
 * times a second. Iterating the world for them is the exact pattern the project bans: it
 * scales with the whole level rather than with the handful of ships that exist.
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
	UNavalVesselComponent* FindNearestVessel(const FVector& Location, int32 TeamFilter = INDEX_NONE) const;

	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TArray<TWeakObjectPtr<UNavalVesselComponent>> Vessels;
};
