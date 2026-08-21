// Copyright Epic Games, Inc. All Rights Reserved.

#include "Build/OceanAdventureGameplayAbility_RemovePiece.h"

#include "Building/BuildGameplayTags.h"
#include "Building/BuildPreviewComponent.h"
#include "Building/BuildStructureComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OceanAdventureGameplayAbility_RemovePiece)

bool UOceanAdventureGameplayAbility_RemovePiece::BuildLocalTargetData(
	FOceanAdventureBuildTargetData& OutData,
	FGameplayTag& OutFailReason) const
{
	const UBuildPreviewComponent* Preview = GetPreviewComponent();
	UBuildStructureComponent* Structure = Preview ? Preview->GetCurrentStructure() : nullptr;
	if (!Structure)
	{
		OutFailReason = BuildGameplayTags::Fail_BadDefinition;
		return false;
	}

	OutData.HostActor = Structure->GetOwner();
	OutData.SlotKey = Preview->GetCurrentSlot();
	OutData.PieceIndex = static_cast<uint16>(FMath::Max(0, Preview->GetSelectedPieceIndex()));

	// Removal wants an occupied cell, which is exactly where placement would be refused.
	if (!Structure->QueryPiece(OutData.SlotKey))
	{
		OutFailReason = BuildGameplayTags::Fail_NotFound;
		return false;
	}
	return true;
}

bool UOceanAdventureGameplayAbility_RemovePiece::ApplyToStructure(
	UBuildStructureComponent* Structure,
	const FOceanAdventureBuildTargetData& Data,
	FGameplayTag& OutFailReason)
{
	if (!Structure->TryRemovePiece(Data.SlotKey, GetControllerFromActorInfo()))
	{
		OutFailReason = BuildGameplayTags::Fail_WouldOrphan;
		return false;
	}
	return true;
}
