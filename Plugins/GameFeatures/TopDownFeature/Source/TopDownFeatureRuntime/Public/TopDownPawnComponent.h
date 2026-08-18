// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PawnComponent.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"

#include "TopDownPawnComponent.generated.h"

class APlayerController;
class UEnhancedInputComponent;
struct FComponentRequestHandle;
struct FInputActionValue;

/** Adds optional cursor click movement to an existing Lyra pawn and movement component. */
UCLASS(Blueprintable, ClassGroup = (TopDown), meta = (BlueprintSpawnableComponent))
class TOPDOWNFEATURERUNTIME_API UTopDownPawnComponent : public UPawnComponent
{
	GENERATED_BODY()

public:
	UTopDownPawnComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Traces from the mouse cursor and starts moving toward the first blocking hit. */
	UFUNCTION(BlueprintCallable, Category = "Top Down|Movement")
	bool SetMoveTargetUnderCursor();

	/** Starts direct movement toward a world-space target without pathfinding. */
	UFUNCTION(BlueprintCallable, Category = "Top Down|Movement")
	void SetMoveTarget(const FVector& TargetLocation);

	UFUNCTION(BlueprintCallable, Category = "Top Down|Movement")
	void CancelMoveToTarget();

	UFUNCTION(BlueprintPure, Category = "Top Down|Movement")
	bool HasMoveTarget() const { return bHasMoveTarget; }

	UFUNCTION(BlueprintPure, Category = "Top Down|Movement")
	FVector GetMoveTarget() const { return MoveTarget; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void HandlePawnExtension(AActor* Actor, FName EventName);
	void BindInputIfReady();
	void UnbindInput();
	void Input_TopDownClick(const FInputActionValue& InputActionValue);

	/** Native action in the active PawnData InputConfig that represents a world click. */
	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input", meta = (Categories = "InputTag"))
	FGameplayTag ClickInputTag;

	/** Show the hardware cursor while click movement is bound. */
	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input")
	bool bShowMouseCursor;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Trace")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Trace", meta = (ClampMin = "1.0", UIMin = "1000.0"))
	float MaxGroundTraceDistance;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Trace")
	bool bTraceComplex;

	/** Direct movement stops once the pawn is within this 2D distance of its target. */
	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Movement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AcceptanceRadius;

	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> BoundInputComponent;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> CursorController;

	TArray<uint32> InputBindingHandles;
	TSharedPtr<FComponentRequestHandle> ExtensionRequestHandle;
	FVector MoveTarget;
	bool bHasMoveTarget;
	bool bInputBound;
	bool bPreviousShowMouseCursor;
};
