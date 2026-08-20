// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "OceanChunkActor.generated.h"

class AOceanChunkActor;
class UOceanGenerationSettings;
class USceneComponent;

USTRUCT(BlueprintType)
struct OCEANCORERUNTIME_API FOceanChunkState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
	FIntPoint ChunkCoord = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
	float ChunkSize = 20000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
	int32 WorldSeed = 12345;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
	float BaseZ = 0.0f;

	/** Optional immutable generation parameters used to build this chunk's presentation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Generation")
	TObjectPtr<UOceanGenerationSettings> GenerationSettings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
	bool bInitialized = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOceanChunkInitializedSignature, AOceanChunkActor*, Chunk);

/**
 * Replicated runtime container for one ocean world chunk.
 * The server initializes the chunk once; clients deterministically build presentation from the replicated state.
 */
UCLASS(BlueprintType, Blueprintable)
class OCEANCORERUNTIME_API AOceanChunkActor : public AActor
{
	GENERATED_BODY()

public:
	AOceanChunkActor();

	virtual void PreInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ocean|Chunk")
	bool InitializeChunk(
		FIntPoint InChunkCoord,
		float InChunkSize,
		int32 InWorldSeed,
		float InBaseZ = 0.0f,
		UOceanGenerationSettings* InGenerationSettings = nullptr);

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	FIntPoint GetChunkCoord() const { return ChunkState.ChunkCoord; }

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	/** Returns the immutable chunk-size snapshot replicated when this chunk was created. */
	float GetChunkSize() const { return ChunkState.ChunkSize; }

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	int32 GetWorldSeed() const { return ChunkState.WorldSeed; }

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	float GetBaseZ() const { return ChunkState.BaseZ; }

	UFUNCTION(BlueprintPure, Category = "Ocean|Generation")
	UOceanGenerationSettings* GetGenerationSettings() const { return ChunkState.GenerationSettings; }

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	int32 GetChunkSeed() const;

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	FVector GetChunkWorldOrigin() const;

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	bool IsChunkInitialized() const { return ChunkState.bInitialized; }

	UPROPERTY(BlueprintAssignable, Category = "Ocean|Chunk")
	FOceanChunkInitializedSignature OnChunkInitialized;

protected:
	UFUNCTION()
	void OnRep_ChunkState();

	UFUNCTION(BlueprintImplementableEvent, Category = "Ocean|Chunk", meta = (DisplayName = "On Chunk Initialized"))
	void BP_OnChunkInitialized();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(ReplicatedUsing = OnRep_ChunkState, VisibleInstanceOnly, BlueprintReadOnly, Category = "Ocean|Chunk")
	FOceanChunkState ChunkState;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean|Debug")
	bool bDrawDebugBounds = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean|Debug")
	FLinearColor DebugColor = FLinearColor(0.0f, 0.65f, 1.0f, 1.0f);

private:
	void NotifyChunkInitialized();

	bool bBeginPlayFinished = false;
	bool bInitializationNotified = false;
};
