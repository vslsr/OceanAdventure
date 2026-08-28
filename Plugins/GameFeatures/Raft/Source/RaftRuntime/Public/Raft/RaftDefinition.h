// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Naval/NavalCoreTypes.h"

#include "RaftDefinition.generated.h"

class UBuildPieceCatalog;

/** Shared build catalog, buoyancy tuning and direct-helm rules for one raft family. */
UCLASS(BlueprintType, Const)
class RAFTRUNTIME_API URaftDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	URaftDefinition();

	UBuildPieceCatalog* GetBuildPieceCatalog() const { return BuildPieceCatalog; }
	const TArray<FVector>& GetPontoonOffsets() const { return PontoonOffsets; }
	float GetWaterlineOffset() const { return WaterlineOffset; }
	float GetMaxTiltDegrees() const { return FMath::Clamp(MaxTiltDegrees, 0.0f, 20.0f); }
	float GetVerticalInterpSpeed() const { return FMath::Max(0.0f, VerticalInterpSpeed); }
	float GetRotationInterpSpeed() const { return FMath::Max(0.0f, RotationInterpSpeed); }
	ENavalMovementModel GetMovementModel() const { return MovementModel; }
	bool AllowsDirectHelmInteraction() const { return bAllowDirectHelmInteraction; }
	float GetDirectHelmInteractionRange() const { return FMath::Max(50.0f, DirectHelmInteractionRange); }

protected:
	/** Raft-owned append-only network catalog for all pieces this family can build. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Build")
	TObjectPtr<UBuildPieceCatalog> BuildPieceCatalog;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy")
	TArray<FVector> PontoonOffsets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy", meta = (Units = "cm"))
	float WaterlineOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy",
		meta = (ClampMin = "0.0", ClampMax = "20.0", Units = "deg"))
	float MaxTiltDegrees = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy", meta = (ClampMin = "0.0"))
	float VerticalInterpSpeed = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Buoyancy", meta = (ClampMin = "0.0"))
	float RotationInterpSpeed = 2.0f;

	/** Selects the generic NavalCore movement integrator for this raft family. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Movement")
	ENavalMovementModel MovementModel = ENavalMovementModel::Helm;

	/** Compact vessels can expose their hull as the helm station without spawning a console. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Helm")
	bool bAllowDirectHelmInteraction = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Helm", meta = (ClampMin = "50.0", Units = "cm"))
	float DirectHelmInteractionRange = 260.0f;
};
