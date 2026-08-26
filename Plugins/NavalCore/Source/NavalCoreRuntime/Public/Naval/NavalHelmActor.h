// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"

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
 * Ownership, capture and control still live on UNavalHelmComponent, which is one per vessel --
 * this Actor never becomes a second source of truth for who is steering.
 *
 * A vessel may carry several of these: one spawned with the hull, more built onto the deck,
 * and any of them can be picked up and moved. So the wheel a player is standing at is what
 * answers "close enough?" and "is this thing intact?", and the vessel-wide questions -- team,
 * wreck, is the seat already taken -- stay on the component. Which vessel this belongs to is
 * read from what it is attached to, never cached, so carrying one off a deck takes its
 * steering with it without anything having to be told.
 */
UCLASS(BlueprintType, Blueprintable)
class NAVALCORERUNTIME_API ANavalHelmActor : public AActor
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
	UNavalHelmComponent* GetHelmComponent() const;

	/** Whoever the helm believes is steering, or null. */
	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	AActor* GetHelmOperator() const;

	/**
	 * Everything that can refuse this wheel, in one place both the client preview and the
	 * server verdict call: reach and damage are this Actor's, team and occupancy are the
	 * vessel's.
	 */
	UFUNCTION(BlueprintCallable, Category = "Naval|Helm")
	bool CanOperate(const AActor* Candidate, FGameplayTag& OutFailReason) const;

	/** Server side. Takes this wheel for the operator, re-validating everything first. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Helm")
	bool TryOccupy(AActor* NewOperator);

	/** Server side. Frees the wheel if this operator holds it. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Helm")
	void ReleaseOperator(AActor* LeavingOperator);

	/** Where the character is placed and pinned while it holds the wheel. */
	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	FTransform GetOperatorTransform() const;

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

	/** 加固舵芯座: the wide, low body that shots actually have to reach. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TObjectPtr<UBoxComponent> CoreSeatCollision;

	/**
	 * Part component, not a default subobject by accident: the helm core is what the actor
	 * is, so it cannot be injected per experience the way optional abilities are.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TObjectPtr<UNavalPartComponent> CorePart;

	/**
	 * Reach from this wheel. It matches the heavy weapon's and the carryable's so that "close
	 * enough to steer" and "close enough to lift it" cannot disagree by a few centimetres.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (ClampMin = "50.0", Units = "cm"))
	float InteractionRange = 260.0f;

	/** Wheel rotation at full steer. Reads as "hard over", it is not a physical limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (Units = "deg"))
	float WheelFullSteerDegrees = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (ClampMin = "0.1"))
	float WheelInterpSpeed = 6.0f;

private:
	/** Current visual angle of the wheel, interpolated towards the helm's steer intent. */
	float WheelAngleDegrees = 0.0f;
};
