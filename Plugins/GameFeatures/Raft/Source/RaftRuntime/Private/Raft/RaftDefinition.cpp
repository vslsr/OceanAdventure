// Copyright Epic Games, Inc. All Rights Reserved.

#include "Raft/RaftDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RaftDefinition)

URaftDefinition::URaftDefinition()
{
	PontoonOffsets = {
		FVector(80.0, 80.0, 0.0),
		FVector(-80.0, 80.0, 0.0),
		FVector(80.0, -80.0, 0.0),
		FVector(-80.0, -80.0, 0.0)
	};
}
