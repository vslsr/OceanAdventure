// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "NavalHelmStation.generated.h"

class UNavalHelmComponent;
class UNavalPartComponent;
struct FGameplayTag;

/**
 * Common interaction surface for anything that can put a player at a vessel's helm.
 *
 * A normal ship exposes this through a placed ANavalHelmActor. A compact life raft exposes
 * it through the hull itself. Keeping this contract in NavalCore lets the gameplay ability
 * support both without depending on the Raft GameFeature.
 */
UINTERFACE(MinimalAPI, NotBlueprintable)
class UNavalHelmStation : public UInterface
{
	GENERATED_BODY()
};

class NAVALCORERUNTIME_API INavalHelmStation
{
	GENERATED_BODY()

public:
	virtual UNavalHelmComponent* GetHelmComponent() const = 0;
	virtual UNavalPartComponent* GetHelmCorePart() const = 0;
	virtual bool CanOperate(const AActor* Candidate, FGameplayTag& OutFailReason) const = 0;
	virtual bool TryOccupy(AActor* NewOperator) = 0;
	virtual void ReleaseOperator(AActor* LeavingOperator) = 0;
	virtual FTransform GetOperatorTransform() const = 0;
	virtual FVector GetInteractionLocation() const = 0;
	virtual bool IsWithinInteractionRange(const AActor* Candidate) const = 0;
};
