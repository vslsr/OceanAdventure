// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "TimerManager.h"

#include "OceanWorldManagerComponent.generated.h"

class AOceanChunkActor;
class UOceanChunkInvokerComponent;
class UOceanGenerationSettings;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOceanChunkLifecycleSignature, AOceanChunkActor*, Chunk);

/** Server-authoritative owner of the active ocean chunk set for one UWorld. */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Ocean), meta = (BlueprintSpawnableComponent))
class OCEANCORERUNTIME_API UOceanWorldManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOceanWorldManagerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Finds the manager component on the current GameState. */
	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk", meta = (WorldContext = "WorldContextObject"))
	static UOceanWorldManagerComponent* Get(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ocean|Chunk")
	void RegisterInvoker(UOceanChunkInvokerComponent* Invoker);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ocean|Chunk")
	void UnregisterInvoker(UOceanChunkInvokerComponent* Invoker);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ocean|Chunk")
	void RequestRefreshChunks();

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	FIntPoint GetChunkCoordFromWorldLocation(FVector WorldLocation) const;

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	FVector GetChunkWorldOrigin(FIntPoint ChunkCoord) const;

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	AOceanChunkActor* GetChunkAtCoord(FIntPoint ChunkCoord) const;

	UFUNCTION(BlueprintPure, BlueprintAuthorityOnly, Category = "Ocean|Chunk")
	int32 GetActiveChunkCount() const;

	UFUNCTION(BlueprintPure, BlueprintAuthorityOnly, Category = "Ocean|Chunk")
	TArray<FIntPoint> GetActiveChunkCoords() const;

	UFUNCTION(BlueprintPure, BlueprintAuthorityOnly, Category = "Ocean|Chunk")
	int32 GetRegisteredInvokerCount() const;

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	/** Returns the manager's current sanitized chunk-size configuration. */
	float GetChunkSize() const { return GetSafeChunkSize(); }

	UFUNCTION(BlueprintPure, Category = "Ocean|Generation")
	int32 GetWorldSeed() const { return WorldSeed; }

	UFUNCTION(BlueprintPure, Category = "Ocean|Generation")
	UOceanGenerationSettings* GetGenerationSettings() const { return GenerationSettings; }

	UPROPERTY(BlueprintAssignable, Category = "Ocean|Chunk")
	FOceanChunkLifecycleSignature OnChunkActivated;

	UPROPERTY(BlueprintAssignable, Category = "Ocean|Chunk")
	FOceanChunkLifecycleSignature OnChunkDeactivated;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk")
	TSubclassOf<AOceanChunkActor> ChunkClass;

	/** Optional shared asset. When assigned, it supplies WorldSeed and ChunkSize at startup. */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Ocean|Generation")
	TObjectPtr<UOceanGenerationSettings> GenerationSettings;

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Ocean|Generation",
		meta = (EditCondition = "GenerationSettings == nullptr"))
	int32 WorldSeed = 12345;

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Ocean|Chunk",
		meta = (ClampMin = "100.0", EditCondition = "GenerationSettings == nullptr"))
	float ChunkSize = 20000.0f;

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Ocean|Chunk")
	float ChunkBaseZ = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk", meta = (ClampMin = "0.05"))
	float RefreshInterval = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Chunk", meta = (ClampMin = "0.0"))
	float UnloadGraceSeconds = 10.0f;

private:
	float GetSafeChunkSize() const { return FMath::Max(100.0f, ChunkSize); }
	bool HasAuthority() const;
	void RefreshChunks();
	void CleanupInvalidReferences();
	TSet<FIntPoint> BuildRequiredChunkSet() const;
	AOceanChunkActor* SpawnChunk(FIntPoint ChunkCoord);
	void DestroyChunk(FIntPoint ChunkCoord);
	void LogMissingChunkClass();

	TArray<TWeakObjectPtr<UOceanChunkInvokerComponent>> Invokers;
	TMap<FIntPoint, TWeakObjectPtr<AOceanChunkActor>> ActiveChunks;
	TMap<FIntPoint, double> PendingUnloadDeadlines;
	FTimerHandle RefreshTimer;
	bool bRefreshInProgress = false;
	bool bRefreshRequested = false;
	bool bRefreshQueued = false;
	bool bLoggedMissingChunkClass = false;
};
