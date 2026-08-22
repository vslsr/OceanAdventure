// Copyright Epic Games, Inc. All Rights Reserved.

#include "Build/RaftBuildPieceDefinition.h"

#include "NativeGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RaftBuildPieceDefinition)

namespace RaftBuildTags
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PieceFoundationWood, "Raft.Piece.Foundation.Wood");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PieceFloorDeck, "Raft.Piece.Floor.Deck");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PiecePropCampfire, "Raft.Piece.Prop.Campfire");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PieceWallPlain, "Raft.Piece.Wall.Plain");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PieceWallFireWindow, "Raft.Piece.Wall.FireWindow");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PiecePropPontoon, "Raft.Piece.Prop.Pontoon");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PiecePropThruster, "Raft.Piece.Prop.Thruster");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PiecePropRudder, "Raft.Piece.Prop.Rudder");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PiecePropHeavyCannon, "Raft.Piece.Prop.HeavyCannon");
}

URaftBuildPieceDefinition::URaftBuildPieceDefinition()
{
	PieceTag = RaftBuildTags::PieceFoundationWood;
	SlotType = EBuildSlotType::Foundation;
	Footprint = FIntPoint(1, 1);
	// Mesh and alignment come from the Raft-owned asset script. Runtime defaults must never
	// resize a configured raft mesh into a generic square-grid tile.
	MeshScale = FVector::OneVector;
	MeshOffset = FVector::ZeroVector;
}

void URaftBuildPieceDefinition::PostLoad()
{
	Super::PostLoad();

	// Assets created before the native tag was registered may have serialized an empty
	// value over the class default. Repair those assets in memory and on the next save.
	if (!PieceTag.IsValid())
	{
		PieceTag = RaftBuildTags::PieceFoundationWood;
	}
}
