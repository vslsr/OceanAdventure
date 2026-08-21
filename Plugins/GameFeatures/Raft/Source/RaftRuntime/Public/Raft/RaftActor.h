// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/BuildStructureHost.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "RaftActor.generated.h"

class UBoxComponent;
class UBuildStructureComponent;
class UBuildStructureVisualComponent;
class URaftBuoyancyComponent;
class URaftDefinition;
class USceneComponent;
class UStaticMeshComponent;

/** Replicated moving platform whose transform is authored by server-only buoyancy. */
UCLASS(BlueprintType, Blueprintable)
class RAFTRUNTIME_API ARaftActor : public AActor, public IBuildStructureHost
{
	GENERATED_BODY()

public:
	ARaftActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;

	UFUNCTION(BlueprintPure, Category = "Raft")
	UBoxComponent* GetDeckCollision() const { return DeckCollision; }

	UFUNCTION(BlueprintPure, Category = "Raft")
	UStaticMeshComponent* GetVisualMesh() const { return VisualMesh; }

	UFUNCTION(BlueprintPure, Category = "Raft")
	URaftBuoyancyComponent* GetBuoyancyComponent() const { return BuoyancyComponent; }

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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft")
	TObjectPtr<URaftDefinition> RaftDefinition;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Raft|Collision")
	TObjectPtr<UBoxComponent> DeckCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Raft|Presentation")
	TObjectPtr<USceneComponent> VisualPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Raft|Presentation")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy")
	TObjectPtr<URaftBuoyancyComponent> BuoyancyComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Raft|Build")
	TObjectPtr<UBuildStructureComponent> BuildStructureComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Raft|Build")
	TObjectPtr<UBuildStructureVisualComponent> BuildStructureVisualComponent;

	/**
	 * CellSize / LevelHeight are authored; CellOrigin and BaseHeight are derived from the deck
	 * by RecomputeGridAlignment() so that whole cells always fit inside the deck footprint.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Build")
	FBuildGridSettings BuildGridSettings;

	/** Inclusive level-0 cell index range covered by the base deck. */
	FIntPoint AnchorMin = FIntPoint(0, 0);
	FIntPoint AnchorMax = FIntPoint(-1, -1);

private:
	void ApplyDefinition();

	/** Aligns the grid to the deck and caches the anchored cell range. */
	void RecomputeGridAlignment();

	FVector GetBaseDeckExtent() const;
};
