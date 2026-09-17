// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PawnComponent.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"

#include "TopDownPawnComponent.generated.h"

class APlayerController;
class UCharacterMovementComponent;
class UCommonActivatableWidget;
class UEnhancedInputComponent;
struct FComponentRequestHandle;
struct FInputActionValue;

/** Adds mouse-facing WASD movement and local camera input to an existing Lyra pawn. */
UCLASS(Blueprintable, ClassGroup = (TopDown), meta = (BlueprintSpawnableComponent))
class TOPDOWNFEATURERUNTIME_API UTopDownPawnComponent : public UPawnComponent
{
	GENERATED_BODY()

public:
	UTopDownPawnComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Legacy Blueprint API; no input action invokes click-to-move after the WASD migration. */
	UFUNCTION(BlueprintCallable, Category = "Top Down|Movement")
	bool SetMoveTargetUnderCursor();

	/** Legacy direct movement API retained for existing Blueprint callers. */
	UFUNCTION(BlueprintCallable, Category = "Top Down|Movement")
	void SetMoveTarget(const FVector& TargetLocation);

	UFUNCTION(BlueprintCallable, Category = "Top Down|Movement")
	void CancelMoveToTarget();

	UFUNCTION(BlueprintPure, Category = "Top Down|Movement")
	bool HasMoveTarget() const { return bHasMoveTarget; }

	UFUNCTION(BlueprintPure, Category = "Top Down|Movement")
	FVector GetMoveTarget() const { return MoveTarget; }

	float GetCameraDistance() const { return CameraDistance; }
	float GetCameraYawOffset() const { return CameraYawOffset; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void HandlePawnExtension(AActor* Actor, FName EventName);
	void BindInputIfReady();
	void UnbindInput();
	void Input_MoveForward(const FInputActionValue& InputActionValue);
	void Input_MoveBackward(const FInputActionValue& InputActionValue);
	void Input_MoveRight(const FInputActionValue& InputActionValue);
	void Input_MoveLeft(const FInputActionValue& InputActionValue);
	void Input_CameraZoom(const FInputActionValue& InputActionValue);
	void Input_CameraRotateStarted(const FInputActionValue& InputActionValue);
	void Input_CameraRotateCompleted(const FInputActionValue& InputActionValue);
	void Input_CameraRotate(const FInputActionValue& InputActionValue);
	void Input_SprintStarted(const FInputActionValue& InputActionValue);
	void Input_SprintCompleted(const FInputActionValue& InputActionValue);
	void SetSprinting(bool bNewSprinting);
	void ApplySprintSpeed();
	void CaptureBaseMaxWalkSpeed();
	UCharacterMovementComponent* FindCharacterMovement() const;

	/**
	 * The owning client decides when it sprints; the server has to agree or its own
	 * simulation keeps walking at base speed and corrects the client back every update.
	 * Simulated proxies need nothing: they already see the replicated movement.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetSprinting(bool bNewSprinting);

	void EnsureInputWidget(APlayerController* PlayerController);
	void RemoveInputWidget();
	void PushCameraDragInputWidget(APlayerController* PlayerController);
	void PopCameraDragInputWidget();
	void UpdateFacingFromMouse(float DeltaTime);

	/** Legacy tag retained for Blueprint/data compatibility; it is no longer bound to input. */
	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input", meta = (Categories = "InputTag"))
	FGameplayTag ClickInputTag;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input", meta = (Categories = "InputTag"))
	FGameplayTag MoveForwardInputTag;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input", meta = (Categories = "InputTag"))
	FGameplayTag MoveBackwardInputTag;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input", meta = (Categories = "InputTag"))
	FGameplayTag MoveRightInputTag;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input", meta = (Categories = "InputTag"))
	FGameplayTag MoveLeftInputTag;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input", meta = (Categories = "InputTag"))
	FGameplayTag CameraZoomInputTag;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input", meta = (Categories = "InputTag"))
	FGameplayTag CameraRotateHoldInputTag;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input", meta = (Categories = "InputTag"))
	FGameplayTag CameraRotateInputTag;

	/**
	 * Optional. InputConfigs without a sprint entry (SimpleExperience still uses
	 * DA_InputConfig_Base) keep every other top-down binding; only sprint goes missing.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input", meta = (Categories = "InputTag"))
	FGameplayTag SprintInputTag;

	/** CommonUI policy widget that keeps the cursor visible without touching PlayerController state. */
	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input")
	TSubclassOf<UCommonActivatableWidget> InputWidgetClass;

	/** CommonUI policy used only while the rotate button is held, so pointer delta remains available at screen edges. */
	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input")
	TSubclassOf<UCommonActivatableWidget> CameraDragInputWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input", meta = (Categories = "UI.Layer"))
	FGameplayTag UILayerTag;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Trace")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Trace", meta = (ClampMin = "1.0", UIMin = "1000.0"))
	float MaxGroundTraceDistance;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Trace")
	bool bTraceComplex;

	/**
	 * Exponential smoothing rate (per second) for turning the pawn toward the mouse-plane
	 * direction, as in RInterpTo: the step is the remaining angle times Clamp(Speed * Dt, 0, 1),
	 * so it is fast while the error is large and settles without overshoot. Zero snaps.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Movement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FacingRotationInterpSpeed;

	/** Multiplies MaxWalkSpeed while the sprint input is held. */
	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Movement", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float SprintSpeedMultiplier;

	/**
	 * While any of these ASC tags is present, station/building input owns the cursor and the
	 * pawn must not turn toward it.  Defaults to Lyra's canonical movement-stopped state.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Movement")
	FGameplayTagContainer FacingBlockedTags;

	/** Direct movement stops once the pawn is within this 2D distance of its target. */
	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Movement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AcceptanceRadius;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Camera", meta = (ClampMin = "0.0"))
	float InitialCameraDistance;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Camera", meta = (ClampMin = "0.0"))
	float MinCameraDistance;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Camera", meta = (ClampMin = "0.0"))
	float MaxCameraDistance;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Camera", meta = (ClampMin = "0.0"))
	float ZoomUnitsPerStep;

	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Camera")
	float RotationDegreesPerPixel;

	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> BoundInputComponent;

	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidget> PushedInputWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidget> PushedCameraDragInputWidget;

	/** MaxWalkSpeed as the pawn blueprint tuned it, captured before sprint ever scales it. */
	float BaseMaxWalkSpeed;

	TArray<uint32> InputBindingHandles;
	TSharedPtr<FComponentRequestHandle> ExtensionRequestHandle;
	FVector MoveTarget;
	float CameraDistance;
	float CameraYawOffset;
	bool bHasMoveTarget;
	bool bInputBound;
	bool bCameraRotateHeld;
	bool bSprinting;
	bool bBaseMaxWalkSpeedCaptured;
	bool bOriginalUseControllerRotationYaw;
	bool bRotationPolicyOverridden;
};
