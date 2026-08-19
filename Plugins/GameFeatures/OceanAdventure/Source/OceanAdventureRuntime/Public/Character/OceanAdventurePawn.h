// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Character/LyraCharacter.h"

#include "OceanAdventurePawn.generated.h"

class ULyraPawnData;

/**
 * Player pawn base class for the Ocean Adventure experience.
 *
 * Deliberately thin. It only carries what "a character that lives in the ocean" needs
 * (swim-capable movement defaults); it owns no chunk streaming and no camera logic.
 *
 * Everything else is injected: BP_Experience_Ocean enables the game features, and their
 * GameFeatureData actions add the components onto this pawn.
 *
 *   OceanAdventure  -> UOceanChunkInvokerComponent (OceanCore) -- streams the chunks around the player
 *   TopDownFeature  -> UTopDownPawnComponent                   -- click-to-move input
 *
 * The top down camera itself is not a component: it comes from DefaultCameraMode on the
 * experience's PawnData (DA_OceanAdventure_PawnData).
 *
 * Adding or dropping a capability is therefore an edit to the experience, not to this class.
 */
UCLASS(Blueprintable, Meta = (ShortTooltip = "Player pawn base class for the Ocean Adventure experience."))
class OCEANADVENTURERUNTIME_API AOceanAdventurePawn : public ALyraCharacter
{
	GENERATED_BODY()

public:

	AOceanAdventurePawn(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** PawnData this pawn was spawned from, or null before the experience has initialized it. */
	UFUNCTION(BlueprintPure, Category = "Ocean Adventure")
	const ULyraPawnData* GetOceanAdventurePawnData() const;

protected:

	//~AActor interface
	virtual void BeginPlay() override;
	//~End of AActor interface
};
