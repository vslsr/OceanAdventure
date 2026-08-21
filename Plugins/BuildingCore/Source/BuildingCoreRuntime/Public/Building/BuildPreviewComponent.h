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
 * Creative-mode mouse preview used by the P0 GM sandbox.
 * The preview is cosmetic and local; only the server RPC mutates structure truth.
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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Building|Creative")
	void SetBuildModeEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Building|Creative")
	bool IsBuildModeEnabled() const { return bBuildModeEnabled; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Building|Creative")
	void SetSelectedPieceIndex(int32 NewPieceIndex);

	UFUNCTION(BlueprintPure, Category = "Building|Creative")
	int32 GetSelectedPieceIndex() const { return SelectedPieceIndex; }

private:
	UFUNCTION()
	void OnRep_BuildModeEnabled();

	UFUNCTION()
	void OnRep_SelectedPieceIndex();

	UFUNCTION(Server, Reliable)
	void ServerSetBuildModeEnabled(bool bEnabled);

	UFUNCTION(Server, Reliable)
	void ServerTryPlace(AActor* HostActor, FBuildSlotKey Key, uint8 Rotation);

	UFUNCTION(Client, Reliable)
	void ClientPlacementRejected(FGameplayTag FailReason);

	APlayerController* GetLocalPlayerController() const;
	UBuildStructureComponent* FindBuildStructure(APlayerController* PlayerController) const;
	bool ProjectCursorToStructure(
		APlayerController* PlayerController,
		const UBuildStructureComponent* Structure,
		FVector& OutWorldLocation) const;
	void RefreshLocalMode();
	void DestroyPreviewMesh();
	void ConfigurePreviewMesh(const UBuildPieceDefinition* Definition);
	void ApplyPreviewMaterial(
		const UBuildPieceDefinition* Definition,
		bool bPlacementValid,
		float ShakeAmplitude);
	void RestorePieceMaterials(const UBuildPieceDefinition* Definition);
	void UpdatePreview();
	void TriggerFailureFeedback(FGameplayTag FailReason);
	bool IsLocallyWithinRange(
		const UBuildStructureComponent* Structure,
		const FBuildSlotKey& Key) const;

	UPROPERTY(ReplicatedUsing = OnRep_BuildModeEnabled)
	bool bBuildModeEnabled = false;

	UPROPERTY(ReplicatedUsing = OnRep_SelectedPieceIndex)
	uint16 SelectedPieceIndex = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Building|Creative", meta = (ClampMin = "100.0", Units = "cm"))
	double HostSearchRadius = 2500.0;

	UPROPERTY(EditDefaultsOnly, Category = "Building|Creative", meta = (ClampMin = "100.0", Units = "cm"))
	double LocalPlacementDistance = 1500.0;

	UPROPERTY(EditDefaultsOnly, Category = "Building|Creative", meta = (ClampMin = "0.0", Units = "cm"))
	float FailureShakeAmplitude = 14.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Building|Creative", meta = (ClampMin = "0.01", Units = "s"))
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
	bool bCurrentPlacementValid = false;
	bool bUsingInvalidPreviewMaterial = false;
	bool bPreviousShowMouseCursor = false;
	bool bCapturedCursorState = false;
	double FailureShakeEndSeconds = 0.0;
};
