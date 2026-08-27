// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PawnComponent.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"

#include "TopDownPawnComponent.generated.h"

class APlayerController;
class UCommonActivatableWidget;
class UEnhancedInputComponent;
class ULyraInputConfig;
struct FComponentRequestHandle;
struct FInputActionValue;

/** Executes GAS-owned top-down movement intent and owns mouse-facing/camera presentation. */
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

	/** Called by the TopDown movement Ability; the component executes but does not own input. */
	void SetAbilityMoveInput(FGameplayTag InputTag, bool bPressed);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void HandlePawnExtension(AActor* Actor, FName EventName);
	void BindInputIfReady();
	void UnbindInput();
	void Input_CameraZoom(const FInputActionValue& InputActionValue);
	void Input_CameraRotateStarted(const FInputActionValue& InputActionValue);
	void Input_CameraRotateCompleted(const FInputActionValue& InputActionValue);
	void Input_CameraRotate(const FInputActionValue& InputActionValue);
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

	/** Feature-owned config used only to resolve camera-native actions by tag. */
	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Input")
	TSoftObjectPtr<ULyraInputConfig> TopDownInputConfig;

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

	/** Degrees per second used to turn the pawn toward the mouse-plane direction. Zero snaps. */
	UPROPERTY(EditDefaultsOnly, Category = "Top Down|Movement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FacingRotationInterpSpeed;

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

	TArray<uint32> InputBindingHandles;
	TSharedPtr<FComponentRequestHandle> ExtensionRequestHandle;
	FGameplayTagContainer ActiveAbilityMoveInputs;
	FVector MoveTarget;
	float CameraDistance;
	float CameraYawOffset;
	bool bHasMoveTarget;
	bool bInputBound;
	bool bCameraRotateHeld;
	bool bOriginalUseControllerRotationYaw;
	bool bRotationPolicyOverridden;
};
