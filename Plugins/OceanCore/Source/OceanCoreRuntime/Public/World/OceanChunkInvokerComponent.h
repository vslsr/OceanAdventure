// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "TimerManager.h"

#include "OceanChunkInvokerComponent.generated.h"

class UOceanWorldManagerComponent;

/** Marks a replicated Pawn or boat as a source of required ocean chunks. */
UCLASS(ClassGroup = (Ocean), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class OCEANCORERUNTIME_API UOceanChunkInvokerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOceanChunkInvokerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Applies immediately on authority; an owning client forwards the bounded request to the server. */
	UFUNCTION(BlueprintCallable, Category = "Ocean|Chunk")
	void SetActiveRadius(int32 InActiveRadius);

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	int32 GetActiveRadius() const { return ActiveRadius; }

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	float GetActiveRangeCentimeters(float ChunkSize) const;

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	FVector GetInvokerLocation() const;

	UFUNCTION(BlueprintPure, Category = "Ocean|Chunk")
	bool IsRegisteredWithManager() const;

protected:
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_ActiveRadius, BlueprintReadOnly, Category = "Ocean|Chunk", meta = (ClampMin = "0", ClampMax = "16"))
	int32 ActiveRadius = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean|Chunk", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MinActiveRadius = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean|Chunk", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MaxActiveRadius = 8;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean|Chunk", meta = (ClampMin = "0.1"))
	float RegistrationRetryInterval = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ocean|Chunk", meta = (ClampMin = "0.0"))
	float RadiusChangeCooldown = 0.5f;

	UFUNCTION()
	void OnRep_ActiveRadius();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetActiveRadius(int32 InActiveRadius);

private:
	int32 ClampActiveRadius(int32 InActiveRadius) const;
	void ApplyActiveRadius(int32 InActiveRadius);
	void TickRegistration();
	void StartRegistrationTimer();
	void StopRegistrationTimer();

	UPROPERTY(Transient)
	TObjectPtr<UOceanWorldManagerComponent> RegisteredManager;

	FTimerHandle RegistrationTimer;
	int32 PendingActiveRadius = INDEX_NONE;
	double LastRadiusRequestTime = TNumericLimits<double>::Lowest();
	double LastRadiusChangeTime = TNumericLimits<double>::Lowest();
};
