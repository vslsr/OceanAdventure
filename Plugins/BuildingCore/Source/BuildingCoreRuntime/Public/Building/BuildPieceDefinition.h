// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/BuildGridTypes.h"
#include "Building/BuildResourceSource.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"

#include "BuildPieceDefinition.generated.h"

class AActor;
class UMaterialInterface;
class UStaticMesh;

UCLASS(Abstract, DefaultToInstanced, EditInlineNew)
class BUILDINGCORERUNTIME_API UBuildPieceFragment : public UObject
{
	GENERATED_BODY()
};

UCLASS(DisplayName = "Placement Rules")
class BUILDINGCORERUNTIME_API UBuildPieceFragment_PlacementRules final : public UBuildPieceFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Placement")
	bool bAllowFloating = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Placement")
	bool bRemovable = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Placement")
	bool bBlockedByPawns = true;
};

UCLASS(DisplayName = "Collision")
class BUILDINGCORERUNTIME_API UBuildPieceFragment_Collision final : public UBuildPieceFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Collision")
	bool bBlocking = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Collision")
	bool bCanCharacterStepUpOn = true;
};

/**
 * Makes the piece spawn a real Actor instead of being drawn as an instance.
 *
 * Only for pieces that carry state or need interaction -- a campfire's fuel, a cannon's
 * loading, a chest's contents. Decorations without state stay instanced, which costs nothing.
 */
UCLASS(DisplayName = "Spawn Actor")
class BUILDINGCORERUNTIME_API UBuildPieceFragment_SpawnActor final : public UBuildPieceFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Spawn")
	TSubclassOf<AActor> ActorClass;

	/** Applied on top of the slot transform, in host space. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Spawn", meta = (Units = "cm"))
	FVector SpawnOffset = FVector::ZeroVector;
};

UCLASS(BlueprintType, Const)
class BUILDINGCORERUNTIME_API UBuildPieceDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Piece")
	FGameplayTag PieceTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Piece")
	EBuildSlotType SlotType = EBuildSlotType::Foundation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Piece")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Piece", meta = (Units = "cm"))
	FVector MeshOffset = FVector::ZeroVector;

	/** Applied after the authored mesh transform; useful for placeholder MVP meshes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Piece")
	FVector MeshScale = FVector::OneVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Piece")
	TArray<TObjectPtr<UMaterialInterface>> OverrideMaterials;

	/**
	 * Optional cosmetic material used only while placement is invalid. The material
	 * should animate World Position Offset and expose a ShakeAmplitude scalar in cm.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Preview")
	TObjectPtr<UMaterialInterface> InvalidPreviewMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Piece")
	FIntPoint Footprint = FIntPoint(1, 1);

	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "Building|Piece")
	TArray<TObjectPtr<UBuildPieceFragment>> Fragments;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Building|Cost")
	TArray<FBuildCost> Costs;

	template <typename T>
	const T* FindFragment() const
	{
		for (const UBuildPieceFragment* Fragment : Fragments)
		{
			if (const T* TypedFragment = Cast<T>(Fragment))
			{
				return TypedFragment;
			}
		}
		return nullptr;
	}
};
