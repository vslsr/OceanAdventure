// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "RaftActor.generated.h"

class UBoxComponent;
class URaftBuoyancyComponent;
class URaftDefinition;
class USceneComponent;
class UStaticMeshComponent;

/** Replicated moving platform whose transform is authored by server-only buoyancy. */
UCLASS(BlueprintType, Blueprintable)
class RAFTRUNTIME_API ARaftActor : public AActor
{
	GENERATED_BODY()

public:
	ARaftActor();

	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintPure, Category = "Raft")
	UBoxComponent* GetDeckCollision() const { return DeckCollision; }

	UFUNCTION(BlueprintPure, Category = "Raft")
	UStaticMeshComponent* GetVisualMesh() const { return VisualMesh; }

	UFUNCTION(BlueprintPure, Category = "Raft")
	URaftBuoyancyComponent* GetBuoyancyComponent() const { return BuoyancyComponent; }

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

private:
	void ApplyDefinition();
};
