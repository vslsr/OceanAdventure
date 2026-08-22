// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/BuildPlacedActor.h"

#include "NavalBuildPieceActor.generated.h"

class UNavalFireWindowComponent;
class UNavalPartComponent;

/**
 * A build piece that can be shot: walls, one-way windows, pontoons, thrusters, rudders.
 *
 * These have to be Actors rather than mesh instances because they carry state -- durability,
 * a construction ramp, and for a window a team and an arc. Plain decking stays instanced and
 * costs nothing.
 *
 * All of the tuning comes from the piece definition's naval fragments, so one Actor class
 * covers every naval piece and the ghost the player previewed cannot disagree with the thing
 * that gets built.
 */
UCLASS(BlueprintType, Blueprintable)
class NAVALCORERUNTIME_API ANavalBuildPieceActor : public ABuildPlacedActor
{
	GENERATED_BODY()

public:
	ANavalBuildPieceActor();

	UFUNCTION(BlueprintPure, Category = "Naval|Piece")
	UNavalPartComponent* GetPart() const { return PartComponent; }

	UFUNCTION(BlueprintPure, Category = "Naval|Piece")
	UNavalFireWindowComponent* GetFireWindow() const { return FireWindowComponent; }

protected:
	virtual void ApplySourceDefinition() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Piece")
	TObjectPtr<UNavalPartComponent> PartComponent;

	/** Only created for pieces whose definition carries the fire-window fragment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Piece")
	TObjectPtr<UNavalFireWindowComponent> FireWindowComponent;

private:
	bool bConstructionStarted = false;
};
