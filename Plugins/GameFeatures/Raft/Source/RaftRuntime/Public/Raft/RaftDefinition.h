// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "RaftDefinition.generated.h"

class UStaticMesh;
class UBuildPieceCatalog;

/** Data-driven shape and buoyancy tuning for one raft family. */
UCLASS(BlueprintType, Const)
class RAFTRUNTIME_API URaftDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	URaftDefinition();

	UStaticMesh* GetVisualMesh() const { return VisualMesh; }
	UBuildPieceCatalog* GetBuildPieceCatalog() const { return BuildPieceCatalog; }
	FVector GetDeckBoxExtent() const { return DeckBoxExtent.ComponentMax(FVector(1.0)); }
	FVector GetVisualMeshOffset() const { return VisualMeshOffset; }
	const TArray<FVector>& GetPontoonOffsets() const { return PontoonOffsets; }
	float GetWaterlineOffset() const { return WaterlineOffset; }
	float GetMaxTiltDegrees() const { return FMath::Clamp(MaxTiltDegrees, 0.0f, 20.0f); }
	float GetVerticalInterpSpeed() const { return FMath::Max(0.0f, VerticalInterpSpeed); }
	float GetRotationInterpSpeed() const { return FMath::Max(0.0f, RotationInterpSpeed); }

protected:
	/** Raft-owned append-only network catalog for all pieces this family can build. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Build")
	TObjectPtr<UBuildPieceCatalog> BuildPieceCatalog;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Presentation")
	TObjectPtr<UStaticMesh> VisualMesh;

	/** Building/hull module collision envelope: 200 x 200 x 150 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Collision", meta = (ClampMin = "1.0", Units = "cm"))
	FVector DeckBoxExtent = FVector(100.0, 100.0, 75.0);

	/** SM_Raft is authored around the deck-collision centre, so no visual correction is needed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raft|Presentation", meta = (Units = "cm"))
	FVector VisualMeshOffset = FVector::ZeroVector;

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
};
