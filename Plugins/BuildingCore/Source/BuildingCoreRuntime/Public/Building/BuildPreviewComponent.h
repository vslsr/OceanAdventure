// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/BuildGridTypes.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"

#include "BuildPreviewComponent.generated.h"

class APlayerController;
class UBuildPieceDefinition;
class UBuildStructureComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * Local, cosmetic placement ghost.
 *
 * It owns no gameplay state: the build mode is a GAS ability, the truth is the host's
 * UBuildStructureComponent, and mouse/cursor policy belongs to CommonUI. This component only
 * answers "where would the piece go, and is that legal" and draws the ghost accordingly.
 */
UCLASS(BlueprintType, ClassGroup = (Building), meta = (BlueprintSpawnableComponent))
class BUILDINGCORERUNTIME_API UBuildPreviewComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBuildPreviewComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Driven by the build-mode ability on the owning client. */
	UFUNCTION(BlueprintCallable, Category = "Building|Preview")
	void SetPreviewEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Building|Preview")
	bool IsPreviewEnabled() const { return bPreviewEnabled; }

	UFUNCTION(BlueprintCallable, Category = "Building|Preview")
	void SetSelectedPieceIndex(int32 NewPieceIndex);

	UFUNCTION(BlueprintPure, Category = "Building|Preview")
	int32 GetSelectedPieceIndex() const { return SelectedPieceIndex; }

	/** Cycles through the host catalog; wraps around. */
	UFUNCTION(BlueprintCallable, Category = "Building|Preview")
	void CycleSelectedPiece(int32 Delta);

	UBuildStructureComponent* GetCurrentStructure() const { return CurrentStructure.Get(); }
	const FBuildSlotKey& GetCurrentSlot() const { return CurrentKey; }
	bool IsPlacementValid() const { return bCurrentPlacementValid; }
	FGameplayTag GetCurrentFailReason() const { return CurrentFailReason; }

	/** Cosmetic shake on the ghost after a rejected click. */
	UFUNCTION(BlueprintCallable, Category = "Building|Preview")
	void TriggerFailureFeedback(FGameplayTag FailReason);

private:
	APlayerController* GetLocalPlayerController() const;
	UBuildStructureComponent* FindBuildStructure(APlayerController* PlayerController) const;
	bool ProjectCursorToStructure(
		APlayerController* PlayerController,
		const UBuildStructureComponent* Structure,
		FVector& OutWorldLocation) const;
	void DestroyPreviewMesh();
	void HidePreview();
	void ConfigurePreviewMesh(const UBuildPieceDefinition* Definition);
	void ApplyPreviewMaterial(
		const UBuildPieceDefinition* Definition,
		bool bPlacementValid,
		float ShakeAmplitude);
	void RestorePieceMaterials(const UBuildPieceDefinition* Definition);
	void UpdatePreview();
	bool IsLocallyWithinRange(
		const UBuildStructureComponent* Structure,
		const FBuildSlotKey& Key) const;

	UPROPERTY(EditDefaultsOnly, Category = "Building|Preview", meta = (ClampMin = "100.0", Units = "cm"))
	double HostSearchRadius = 2500.0;

	/**
	 * How often the expensive host search (cursor trace + registry query) may run. The cheap
	 * movement-base check still runs every frame, so stepping onto a raft is picked up at once.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Building|Preview", meta = (ClampMin = "0.0", Units = "s"))
	double HostSearchInterval = 0.25;

	UPROPERTY(EditDefaultsOnly, Category = "Building|Preview", meta = (ClampMin = "100.0", Units = "cm"))
	double LocalPlacementDistance = 1500.0;

	/**
	 * Fraction of a cell the cursor must travel past the current cell's centre before the
	 * preview switches cells. Without it the buoyant host's motion flips the snapped cell
	 * back and forth every frame.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Building|Preview", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	double SlotSwitchHysteresis = 0.6;

	/** Small lift so the ghost never z-fights the deck or an already placed piece. */
	UPROPERTY(EditDefaultsOnly, Category = "Building|Preview", meta = (ClampMin = "0.0", Units = "cm"))
	double PreviewLiftZ = 1.0;

	UPROPERTY(EditDefaultsOnly, Category = "Building|Preview", meta = (ClampMin = "0.0", Units = "cm"))
	float FailureShakeAmplitude = 14.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Building|Preview", meta = (ClampMin = "0.01", Units = "s"))
	float FailureShakeDuration = 0.35f;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PreviewMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> InvalidPreviewMaterialInstances;

	TWeakObjectPtr<UBuildStructureComponent> CurrentStructure;
	TWeakObjectPtr<const UBuildPieceDefinition> ConfiguredDefinition;
	FBuildSlotKey CurrentKey;
	FVector CurrentCursorWorld = FVector::ZeroVector;
	FGameplayTag CurrentFailReason;
	int32 SelectedPieceIndex = 0;
	bool bPreviewEnabled = false;
	bool bCurrentPlacementValid = false;
	bool bHasCurrentKey = false;
	bool bLastAppliedValid = false;
	bool bHasAppliedMaterialState = false;
	bool bUsingInvalidPreviewMaterial = false;
	double FailureShakeEndSeconds = 0.0;
	double NextHostSearchSeconds = 0.0;
};
