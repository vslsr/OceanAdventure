// Copyright Epic Games, Inc. All Rights Reserved.

#include "Build/RaftBuildPieceDefinition.h"

#include "NativeGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RaftBuildPieceDefinition)

namespace RaftBuildTags
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(PieceFoundationWood, "Raft.Piece.Foundation.Wood");
}

URaftBuildPieceDefinition::URaftBuildPieceDefinition()
{
	PieceTag = RaftBuildTags::PieceFoundationWood;
	SlotType = EBuildSlotType::Foundation;
	Footprint = FIntPoint(1, 1);
	// The engine cube is 100 cm, so this maps it onto exactly one 200 cm build cell.
	MeshScale = FVector(2.0, 2.0, 0.1);
	// Level 0 is the deck's top face (FBuildGridSettings::BaseHeight), so a piece only lifts
	// itself by its own half height. Never encode the deck thickness here: it would silently
	// break every piece the moment a raft family changes its deck.
	MeshOffset = FVector(0.0, 0.0, 5.0);
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
