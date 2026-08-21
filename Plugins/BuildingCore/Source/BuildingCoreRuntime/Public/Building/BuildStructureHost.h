// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/BuildGridTypes.h"
#include "Templates/SubclassOf.h"
#include "UObject/Interface.h"

#include "BuildStructureHost.generated.h"

class UInstancedStaticMeshComponent;
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

	/**
	 * Which instanced-mesh component class the visual layer spawns for this host.
	 *
	 * Defaults to UInstancedStaticMeshComponent, which is what a moving host wants -- a HISM
	 * rebuilds its cluster tree as the component moves. A large static host (an island lot)
	 * should return UHierarchicalInstancedStaticMeshComponent to get per-instance culling
	 * and LOD instead.
	 */
	virtual TSubclassOf<UInstancedStaticMeshComponent> GetInstancedMeshComponentClass() const;
	virtual void OnStructureBoundsChanged(const FBox& LocalBounds) { }
};
