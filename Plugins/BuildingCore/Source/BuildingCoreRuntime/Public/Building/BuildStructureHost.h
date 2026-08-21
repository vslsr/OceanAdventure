// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/BuildGridTypes.h"
#include "UObject/Interface.h"

#include "BuildStructureHost.generated.h"

class USceneComponent;

UINTERFACE(MinimalAPI, NotBlueprintable)
class UBuildStructureHost : public UInterface
{
	GENERATED_BODY()
};

class BUILDINGCORERUNTIME_API IBuildStructureHost
{
	GENERATED_BODY()

public:
	virtual FTransform GetStructureSpace() const = 0;
	virtual USceneComponent* GetStructureAttachRoot() const = 0;
	virtual const FBuildGridSettings& GetGridSettings() const = 0;
	virtual bool IsCellAnchored(const FBuildGridCoord& Coord) const = 0;
	virtual bool CollectAnchorCells(TSet<FBuildGridCoord>& OutCells) const = 0;
	virtual bool RequiresConnectivity() const { return true; }
	virtual void OnStructureBoundsChanged(const FBox& LocalBounds) { }
};
