// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Environment/LineArtEnvironmentState.h"
#include "Subsystems/WorldSubsystem.h"

#include "LineArtEnvironmentSubsystem.generated.h"

class ULineArtPointLightComponent;
class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;

/**
 * Writes one frame of environment truth into the shared line-art parameter collection.
 *
 * This is the port of SkyLand's applyEnvironmentInk() plus the per-frame uniform writes in
 * SceneEnvironmentRuntime. The whole point is that it is a SINGLE writer: every fill,
 * outline and grid material samples the same collection, so day/night, weather and
 * campfire light land on the entire world in the same frame at no draw-call cost.
 *
 * Anything that wants to drive the sky (a weather system, a day/night curve, a cutscene)
 * calls SetEnvironment and stays out of the materials.
 */
UCLASS()
class LINEARTCORERUNTIME_API ULineArtEnvironmentSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Line Art", meta = (WorldContext = "WorldContextObject"))
	static ULineArtEnvironmentSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category = "Line Art")
	void SetEnvironment(const FLineArtEnvironmentState& NewEnvironment);

	UFUNCTION(BlueprintPure, Category = "Line Art")
	const FLineArtEnvironmentState& GetEnvironment() const { return Environment; }

	void RegisterPointLight(ULineArtPointLightComponent* PointLight);
	void UnregisterPointLight(ULineArtPointLightComponent* PointLight);

	/** Number of lights currently registered, whatever their distance. For tests and debug. */
	UFUNCTION(BlueprintPure, Category = "Line Art")
	int32 GetRegisteredPointLightCount() const;

private:
	void WriteCollection();
	void WritePointLights(UMaterialParameterCollectionInstance& Instance);
	FVector GetViewLocation() const;

	/** Both helpers log the first miss per name: a typo must not fail silently. */
	void SetScalar(UMaterialParameterCollectionInstance& Instance, FName Name, float Value);
	void SetVector(UMaterialParameterCollectionInstance& Instance, FName Name, const FLinearColor& Value);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> Collection;

	UPROPERTY()
	FLineArtEnvironmentState Environment;

	TArray<TWeakObjectPtr<ULineArtPointLightComponent>> PointLights;

	/** Parameter names already reported missing, so one typo does not spam every frame. */
	TSet<FName> ReportedMissingParameters;
};
