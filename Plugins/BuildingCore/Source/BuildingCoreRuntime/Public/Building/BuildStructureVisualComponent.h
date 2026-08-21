// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"

#include "BuildStructureVisualComponent.generated.h"

class UBuildPieceDefinition;
class UBuildStructureComponent;
class UInstancedStaticMeshComponent;
class UStaticMesh;

/** Runtime ISM presentation and walkable collision derived from replicated structure truth. */
UCLASS(BlueprintType, ClassGroup = (Building), meta = (BlueprintSpawnableComponent))
class BUILDINGCORERUNTIME_API UBuildStructureVisualComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBuildStructureVisualComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void RebuildInstances();
	UInstancedStaticMeshComponent* FindOrCreateISM(
		UStaticMesh* Mesh,
		const UBuildPieceDefinition* Definition);

	UPROPERTY(Transient)
	TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UInstancedStaticMeshComponent>> MeshToISM;

	TWeakObjectPtr<UBuildStructureComponent> BuildComponent;
};
