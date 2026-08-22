// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "GameplayTagContainer.h"

#include "OceanAdventureGameplayAbility_BuildMode.generated.h"

class UBuildPreviewComponent;
class UCommonActivatableWidget;

/**
 * Build mode: a long-running ability owned by GAS.
 *
 * Activation grants Status.Build.Active (which is what the placement abilities require),
 * pushes the CommonUI input widget that owns cursor policy, and turns on the local ghost.
 * Pressing the same input while it is active cancels the ability. No separate build-mode
 * boolean or structure state lives here.
 */
UCLASS()
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_BuildMode : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_BuildMode(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	/** A new build-input press requests cancellation once that physical press is released. */
	virtual void InputPressed(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

	/** Arms the toggle after the activating press, then completes a later exit request. */
	virtual void InputReleased(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

protected:
	/** Its only job is to declare the FUIInputConfig for build mode. */
	UPROPERTY(EditDefaultsOnly, Category = "Building")
	TSubclassOf<UCommonActivatableWidget> BuildInputWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Building", meta = (Categories = "UI.Layer"))
	FGameplayTag UILayerTag;

	UPROPERTY(EditDefaultsOnly, Category = "Building", meta = (ClampMin = "0"))
	int32 DefaultPieceIndex = 0;

private:
	UBuildPreviewComponent* GetPreviewComponent() const;

	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidget> PushedInputWidget;

	/** False until the physical press that activated this ability has completed. */
	bool bActivationInputReleased = false;

	/** A later press requests exit; cancellation waits for release to prevent reactivation. */
	bool bExitRequested = false;
};
