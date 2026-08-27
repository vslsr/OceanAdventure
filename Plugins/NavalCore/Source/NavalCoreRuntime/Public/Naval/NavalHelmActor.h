// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Naval/NavalHelmStation.h"

#include "NavalHelmActor.generated.h"

class UBoxComponent;
class UNavalHelmComponent;
class UNavalPartComponent;
class USceneComponent;
class UStaticMeshComponent;

/**
 * The deck console players call 主舵台, plus the reinforced seat under it.
 *
 * Two bodies on purpose (design 8.3.1). The wheel is tall, obvious and carries no collision:
 * it exists to be walked up to and to show state, so that hitting a visible wheel from across
 * the water cannot paralyse a whole ship. The wide, low core seat under the deck is the real
 * damage body, with 2.5-3x a normal wall's durability, which usually means an attacker has
 * to break through the hull's outer walls or board before they can touch it.
 *
 * Everything a player touches is here, mirroring ANavalHeavyWeaponActor: the seat body is
 * what the station search overlaps, OperatorPoint is where the character is pinned once it
 * takes the wheel, and the wheel turns with the steering the helm is actually applying.
 * Ownership, capture and control still live on UNavalHelmComponent. Expandable vessels build
 * one of these as a fixed deck piece; this Actor never becomes a second steering truth.
 */
UCLASS(BlueprintType, Blueprintable)
class NAVALCORERUNTIME_API ANavalHelmActor : public AActor, public INavalHelmStation
{
	GENERATED_BODY()

public:
	ANavalHelmActor();

	/**
	 * Registers the console as a game framework component receiver, so a feature can inject a
	 * component onto it instead of the console growing a default subobject for every system
	 * that ever wants a say in it.
	 */
	virtual void PreInitializeComponents() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	UNavalPartComponent* GetCorePart() const { return CorePart; }

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	UStaticMeshComponent* GetConsoleMesh() const { return ConsoleMesh; }

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	UStaticMeshComponent* GetWheelMesh() const { return WheelMesh; }

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	UBoxComponent* GetCoreSeatCollision() const { return CoreSeatCollision; }

	/** The helm component on the vessel this console belongs to. */
	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	virtual UNavalHelmComponent* GetHelmComponent() const override;
	virtual UNavalPartComponent* GetHelmCorePart() const override { return CorePart; }

	/** Whoever the helm believes is steering, or null. */
	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	AActor* GetHelmOperator() const;

	/**
	 * Same question the server answers in UNavalHelmComponent::CanOccupy, asked through the
	 * Actor the player actually walked up to, so a preview and the server share one rule set.
	 */
	UFUNCTION(BlueprintCallable, Category = "Naval|Helm")
	virtual bool CanOperate(const AActor* Candidate, FGameplayTag& OutFailReason) const override;

	/** Takes this placed wheel on the server after re-running the same validation. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Helm")
	virtual bool TryOccupy(AActor* NewOperator) override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Helm")
	virtual void ReleaseOperator(AActor* LeavingOperator) override;

	/** Where the character is placed and pinned while it holds the wheel. */
	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	virtual FTransform GetOperatorTransform() const override;
	virtual FVector GetInteractionLocation() const override { return GetActorLocation(); }
	virtual bool IsWithinInteractionRange(const AActor* Candidate) const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Visual and interaction only -- deliberately not a damage body. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TObjectPtr<UStaticMeshComponent> ConsoleMesh;

	/** Spins with the applied steering; presentation only, never read back as state. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TObjectPtr<USceneComponent> WheelPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TObjectPtr<UStaticMeshComponent> WheelMesh;

	/** Character snap point, adjustable on each helm Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TObjectPtr<USceneComponent> OperatorPoint;

	/** 加固舵芯座: the 160 x 160 x 140 cm authoritative damage/seat envelope. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TObjectPtr<UBoxComponent> CoreSeatCollision;

	/**
	 * Part component, not a default subobject by accident: the helm core is what the actor
	 * is, so it cannot be injected per experience the way optional abilities are.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TObjectPtr<UNavalPartComponent> CorePart;

	/** Wheel rotation at full steer. Reads as "hard over", it is not a physical limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (Units = "deg"))
	float WheelFullSteerDegrees = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (ClampMin = "0.1"))
	float WheelInterpSpeed = 6.0f;

	/** Reach belongs to this placed station, not to the vessel-wide helm component. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (ClampMin = "50.0", Units = "cm"))
	float InteractionRange = 260.0f;

private:
	/** Current visual angle of the wheel, interpolated towards the helm's steer intent. */
	float WheelAngleDegrees = 0.0f;
};
