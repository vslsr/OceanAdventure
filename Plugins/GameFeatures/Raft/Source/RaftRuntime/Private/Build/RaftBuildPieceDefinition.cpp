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
