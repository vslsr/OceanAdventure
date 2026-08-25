// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PawnComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"

#include "OceanAdventureHelmInputComponent.generated.h"

class UInputMappingContext;
class UEnhancedInputComponent;
struct FComponentRequestHandle;
struct FInputActionValue;

/**
 * Captures the two continuous helm actions while the helm ability owns their mapping context.
 *
 * This is a player-side input adapter, not the vessel's authority state.  It is injected by
 * the OceanAdventure GameFeature and only the locally controlled pawn binds the actions.  The
 * ability samples these values at 20 Hz and sends them through GAS target data; the NavalCore
 * helm component remains the server-authoritative writer of throttle and steering intent.
 */
UCLASS(Blueprintable, ClassGroup = (OceanAdventure), meta = (BlueprintSpawnableComponent))
class OCEANADVENTURERUNTIME_API UOceanAdventureHelmInputComponent : public UPawnComponent
{
	GENERATED_BODY()

public:
	UOceanAdventureHelmInputComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Adds IMC_OceanHelm at the configured priority for this local player. Idempotent. */
	void EnableHelmInput();

	/** Removes IMC_OceanHelm and clears held values. Idempotent. */
	void DisableHelmInput();

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval|Helm Input")
	float GetThrottleInput() const { return ThrottleInput; }

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval|Helm Input")
	float GetSteerInput() const { return SteerInput; }

	UFUNCTION(BlueprintPure, Category = "OceanAdventure|Naval|Helm Input")
	bool IsHelmInputEnabled() const { return bHelmInputEnabled; }

	/** Clears stale values when a mapping context is removed before Enhanced Input emits Completed. */
	void ResetInput();

protected:
	/** Set by CreateNavalP0Assets.py and resolved through PawnData's ULyraInputConfig. */
	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Naval|Helm Input", meta = (Categories = "InputTag"))
	FGameplayTag ThrottleInputTag;

	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Naval|Helm Input", meta = (Categories = "InputTag"))
	FGameplayTag SteerInputTag;

	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Naval|Helm Input")
	/** Native fallback keeps the mapping valid after an editor restart; the asset script mirrors it on the CDO. */
	TSoftObjectPtr<UInputMappingContext> HelmMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "OceanAdventure|Naval|Helm Input", meta = (ClampMin = "0"))
	int32 HelmMappingPriority = 2;

private:
	void HandlePawnExtension(AActor* Actor, FName EventName);
	void BindInputIfReady();
	void UnbindInput();

	void Input_Throttle(const FInputActionValue& InputActionValue);
	void Input_Steer(const FInputActionValue& InputActionValue);
	void Input_ThrottleReleased(const FInputActionValue& InputActionValue);
	void Input_SteerReleased(const FInputActionValue& InputActionValue);

	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> BoundInputComponent;

	TArray<uint32> InputBindingHandles;
	TSharedPtr<FComponentRequestHandle> ExtensionRequestHandle;

	float ThrottleInput = 0.0f;
	float SteerInput = 0.0f;
	bool bInputBound = false;
	bool bHelmInputEnabled = false;
};
