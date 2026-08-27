// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/BuildStructureHost.h"
#include "Raft/RaftVesselActor.h"

#include "RaftActor.generated.h"

class UBoxComponent;
class UBuildStructureComponent;
class UBuildStructureVisualComponent;
class USceneComponent;

/** Expandable raft: the water-vehicle base plus construction and attachable content. */
UCLASS(BlueprintType, Blueprintable)
class RAFTRUNTIME_API ARaftActor : public ARaftVesselActor, public IBuildStructureHost
{
	GENERATED_BODY()

public:
	ARaftActor();

	virtual void PostInitializeComponents() override;

	UFUNCTION(BlueprintPure, Category = "Raft|Build")
	UBuildStructureComponent* GetBuildStructureComponent() const { return BuildStructureComponent; }

	// IBuildStructureHost
	virtual FTransform GetStructureSpace() const override { return GetActorTransform(); }
	virtual USceneComponent* GetStructureAttachRoot() const override;
	virtual const FBuildGridSettings& GetGridSettings() const override { return BuildGridSettings; }
	virtual bool IsCellAnchored(const FBuildGridCoord& Coord) const override;
	virtual bool CollectAnchorCells(TSet<FBuildGridCoord>& OutCells) const override;
	virtual bool RequiresConnectivity() const override { return true; }
	virtual void OnStructureBoundsChanged(const FBox& LocalBounds) override;

protected:
	virtual void ApplyDefinition() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Raft|Build")
	TObjectPtr<UBuildStructureComponent> BuildStructureComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Raft|Build")
	TObjectPtr<UBuildStructureVisualComponent> BuildStructureVisualComponent;

	/**
	 * LevelHeight is authored for vertical construction. Horizontal module spacing, origin,
	 * and the level-0 height are always derived from the raft's physical deck bounds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Build")
	FBuildGridSettings BuildGridSettings;

	/** Inclusive level-0 cell index range covered by the base deck. */
	FIntPoint AnchorMin = FIntPoint(0, 0);
	FIntPoint AnchorMax = FIntPoint(-1, -1);

private:
	/** Aligns the fixed 200 cm building/hull snap grid and caches its anchor range. */
	void RecomputeGridAlignment();
};
