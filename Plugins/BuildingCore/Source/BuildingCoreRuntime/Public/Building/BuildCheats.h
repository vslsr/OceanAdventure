// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/BuildGridTypes.h"
#include "GameFramework/CheatManager.h"

#include "BuildCheats.generated.h"

class UBuildPieceDefinition;
class UBuildPreviewComponent;
class UBuildStructureComponent;

/** Console-driven creative building sandbox. */
UCLASS(NotBlueprintable)
class BUILDINGCORERUNTIME_API UBuildCheats final : public UCheatManagerExtension
{
	GENERATED_BODY()

public:
	UBuildCheats();

	/** Enter/leave infinite-material mouse building mode. */
	UFUNCTION(Exec, BlueprintAuthorityOnly)
	void BuildMode(int32 bEnabled = 1);

	/** Alias retained from the design document's P0 command list. */
	UFUNCTION(Exec, BlueprintAuthorityOnly)
	void BuildCreative(int32 bEnabled = 1);

	UFUNCTION(Exec, BlueprintAuthorityOnly)
	void BuildSelect(const FString& PieceTag);

	UFUNCTION(Exec, BlueprintAuthorityOnly)
	void BuildPlace(int32 X = -2147483647, int32 Y = 0, int32 Level = 0);

	UFUNCTION(Exec, BlueprintAuthorityOnly)
	void BuildRemove(int32 X = -2147483647, int32 Y = 0, int32 Level = 0);

	UFUNCTION(Exec, BlueprintAuthorityOnly)
	void BuildFill(int32 SizeX, int32 SizeY);

	UFUNCTION(Exec, BlueprintAuthorityOnly)
	void BuildClear();

	UFUNCTION(Exec)
	void BuildDump();

	UFUNCTION(Exec)
	void BuildDebug(int32 bEnabled);

private:
	UBuildStructureComponent* GetBuildComponent() const;
	UBuildPreviewComponent* FindOrAddPreviewComponent(bool bCreateIfMissing) const;
	const UBuildPieceDefinition* GetSelectedPiece() const;
	bool GetCursorSlot(FBuildSlotKey& OutKey) const;

	FString SelectedPieceTag = TEXT("Raft.Piece.Foundation.Wood");
};
