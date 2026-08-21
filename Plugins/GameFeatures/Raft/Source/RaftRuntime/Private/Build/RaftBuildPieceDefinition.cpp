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
	MeshScale = FVector(2.0, 2.0, 0.1);
	// The generated MVP asset may override this for other raft families. The default
	// matches DA_Raft_Default: a 10 cm board whose top is flush with the deck at Z=27.5.
	MeshOffset = FVector(0.0, 0.0, 22.5);
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
