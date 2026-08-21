// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/BuildPieceCatalog.h"

#include "Building/BuildPieceDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BuildPieceCatalog)

const UBuildPieceDefinition* UBuildPieceCatalog::GetByIndex(int32 Index) const
{
	return Pieces.IsValidIndex(Index) ? Pieces[Index].Get() : nullptr;
}

bool UBuildPieceCatalog::FindIndex(const UBuildPieceDefinition* Definition, uint16& OutIndex) const
{
	const int32 FoundIndex = Pieces.IndexOfByKey(Definition);
	if (FoundIndex == INDEX_NONE || FoundIndex > MAX_uint16)
	{
		return false;
	}

	OutIndex = static_cast<uint16>(FoundIndex);
	return true;
}

const UBuildPieceDefinition* UBuildPieceCatalog::FindByTag(FGameplayTag Tag) const
{
	for (const UBuildPieceDefinition* Definition : Pieces)
	{
		if (Definition && Definition->PieceTag == Tag)
		{
			return Definition;
		}
	}
	return nullptr;
}
