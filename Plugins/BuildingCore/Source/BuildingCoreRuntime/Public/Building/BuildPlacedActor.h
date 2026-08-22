// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"

#include "BuildPlacedActor.generated.h"

class UBuildPieceDefinition;
class UStaticMeshComponent;

/**
 * Minimal Actor for build pieces that carry state.
 *
 * It owns no gameplay -- it exists so a piece with a UBuildPieceFragment_SpawnActor has
 * something to spawn without every project writing the same replication and attachment
 * boilerplate. Derive a Blueprint from it for a real campfire or cannon; the visuals still
 * come from the piece definition, so the ghost and the placed thing cannot drift apart.
 */
UCLASS(BlueprintType, Blueprintable)
class BUILDINGCORERUNTIME_API ABuildPlacedActor : public AActor
{
	GENERATED_BODY()

public:
	ABuildPlacedActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server-side. The definition replicates, so clients apply the same visuals via OnRep. */
	void SetSourceDefinition(const UBuildPieceDefinition* Definition);

	UFUNCTION(BlueprintPure, Category = "Building")
	const UBuildPieceDefinition* GetSourceDefinition() const { return SourceDefinition; }

	UFUNCTION(BlueprintPure, Category = "Building")
	UStaticMeshComponent* GetMeshComponent() const { return MeshComponent; }

protected:
	UFUNCTION()
	void OnRep_SourceDefinition();

	/**
	 * Pushes mesh, scale and materials from the definition onto the component.
	 *
	 * Virtual because the definition arrives after BeginPlay -- the host spawns the Actor and
	 * only then hands it the definition -- so a subclass that reads its own fragments (naval
	 * durability, a fire window's arc) has no earlier hook. Called on the server from
	 * SetSourceDefinition and on clients from OnRep_SourceDefinition, so an override runs on
	 * both sides.
	 */
	virtual void ApplySourceDefinition();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Building")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(ReplicatedUsing = OnRep_SourceDefinition)
	TObjectPtr<const UBuildPieceDefinition> SourceDefinition;
};
