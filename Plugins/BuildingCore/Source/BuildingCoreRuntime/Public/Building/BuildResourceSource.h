// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "BuildResourceSource.generated.h"

USTRUCT(BlueprintType)
struct BUILDINGCORERUNTIME_API FBuildCost
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building")
	TSoftClassPtr<UObject> ItemDefinition;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building", meta = (ClampMin = "1"))
	int32 Count = 1;
};

UINTERFACE(MinimalAPI, NotBlueprintable)
class UBuildResourceSource : public UInterface
{
	GENERATED_BODY()
};

class BUILDINGCORERUNTIME_API IBuildResourceSource
{
	GENERATED_BODY()

public:
	virtual bool HasResources(const TArray<FBuildCost>& Costs) const = 0;
	virtual bool ConsumeResources(const TArray<FBuildCost>& Costs) = 0;
	virtual void RefundResources(const TArray<FBuildCost>& Costs) = 0;
};

/** P0 source: every check succeeds and no inventory is modified. */
UCLASS(NotBlueprintable)
class BUILDINGCORERUNTIME_API UBuildCreativeResourceSource final : public UObject, public IBuildResourceSource
{
	GENERATED_BODY()

public:
	virtual bool HasResources(const TArray<FBuildCost>& Costs) const override { return true; }
	virtual bool ConsumeResources(const TArray<FBuildCost>& Costs) override { return true; }
	virtual void RefundResources(const TArray<FBuildCost>& Costs) override { }
};
