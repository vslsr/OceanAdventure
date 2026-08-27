// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Naval/NavalHelmStation.h"

#include "RaftVesselActor.generated.h"

class UBoxComponent;
class UNavalHelmComponent;
class UNavalPartComponent;
class URaftBuoyancyComponent;
class URaftDefinition;
class USceneComponent;
class UStaticMeshComponent;

/**
 * Common water-vehicle hull: collision, presentation, buoyancy, replication and driving.
 *
 * This class deliberately has no building interface or building components. Compact vessels
 * such as the life raft derive directly from it and therefore cannot accept construction.
 */
UCLASS(BlueprintType, Blueprintable)
class RAFTRUNTIME_API ARaftVesselActor : public AActor, public INavalHelmStation
{
	GENERATED_BODY()

public:
	ARaftVesselActor();

	virtual void PreInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;

	UFUNCTION(BlueprintPure, Category = "Raft")
	UBoxComponent* GetDeckCollision() const { return DeckCollision; }

	UFUNCTION(BlueprintPure, Category = "Raft")
	UStaticMeshComponent* GetVisualMesh() const { return VisualMesh; }

	UFUNCTION(BlueprintPure, Category = "Raft")
	URaftBuoyancyComponent* GetBuoyancyComponent() const { return BuoyancyComponent; }

	// INavalHelmStation. Only definitions that opt in expose direct hull interaction.
	virtual UNavalHelmComponent* GetHelmComponent() const override;
	virtual UNavalPartComponent* GetHelmCorePart() const override { return nullptr; }
	virtual bool CanOperate(const AActor* Candidate, FGameplayTag& OutFailReason) const override;
	virtual bool TryOccupy(AActor* NewOperator) override;
	virtual void ReleaseOperator(AActor* LeavingOperator) override;
	virtual FTransform GetOperatorTransform() const override;
	virtual FVector GetInteractionLocation() const override;
	virtual bool IsWithinInteractionRange(const AActor* Candidate) const override;

protected:
	virtual void ApplyDefinition();

	/** Always the authored hull extent, never an envelope expanded by construction. */
	FVector GetBaseDeckExtent() const;

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
};
