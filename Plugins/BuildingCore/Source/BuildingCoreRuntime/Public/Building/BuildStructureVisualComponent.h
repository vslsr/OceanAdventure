// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/BuildGridTypes.h"
#include "Components/ActorComponent.h"

#include "BuildStructureVisualComponent.generated.h"

class AActor;
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

	/**
	 * Spawns and destroys the Actors for stateful pieces. Authority only -- those Actors
	 * replicate themselves, and a campfire must not lose its fire because the structure
	 * around it changed, so this diffs instead of rebuilding.
	 */
	void RefreshSpawnedActors();
	void DestroySpawnedActors();
	UInstancedStaticMeshComponent* FindOrCreateISM(
		UStaticMesh* Mesh,
		const UBuildPieceDefinition* Definition);

	UPROPERTY(Transient)
	TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UInstancedStaticMeshComponent>> MeshToISM;

	/**
	 * Weak on purpose: the world owns these Actors, and one destroyed elsewhere (damage,
	 * streaming) should leave an empty slot here rather than a dangling pointer.
	 */
	TMap<FBuildSlotKey, TWeakObjectPtr<AActor>> SpawnedActors;

	TWeakObjectPtr<UBuildStructureComponent> BuildComponent;
};
