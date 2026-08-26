// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"

#include "CarryableComponent.generated.h"

class UCarrierComponent;

DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnCarryStateChanged, UCarryableComponent* /*Carryable*/, AActor* /*Carrier*/);

/**
 * Marks one world Actor as something a character can pick up and put back down.
 *
 * Deliberately knows nothing about what it is bolted to. A field gun, a core crate and a
 * cargo box are all "a thing that stops being part of the world for a while", and the rules
 * that differ between them (who may operate it, what it costs to set up again) stay in the
 * systems that own those rules.
 *
 * Server authoritative: only the server writes Carrier and RestLocation. Clients replay the
 * same attach/detach from the replicated state, so the attachment does not have to travel
 * through movement replication -- these actors deliberately do not replicate movement.
 */
UCLASS(BlueprintType, ClassGroup = (Carry), meta = (BlueprintSpawnableComponent))
class CARRYCORERUNTIME_API UCarryableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCarryableComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The carryable on Actor, if it has one. Null is the normal answer, not an error. */
	UFUNCTION(BlueprintPure, Category = "Carry")
	static UCarryableComponent* FindCarryable(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Carry")
	bool IsCarried() const { return Carrier != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Carry")
	AActor* GetCarrier() const { return Carrier; }

	UFUNCTION(BlueprintPure, Category = "Carry")
	FGameplayTag GetCarryClass() const { return CarryClass; }

	UFUNCTION(BlueprintPure, Category = "Carry")
	float GetPickupRange() const { return PickupRange; }

	/** How far a character has to end up from the object once it is back on the ground. */
	UFUNCTION(BlueprintPure, Category = "Carry")
	float GetPutDownClearanceRadius() const { return PutDownClearanceRadius; }

	/** Local offset applied after attaching, so a gun sits on the shoulder rather than in it. */
	UFUNCTION(BlueprintPure, Category = "Carry")
	FTransform GetCarryOffsetTransform() const;

	/**
	 * The carryable half of the decision, run identically on the requesting client and on the
	 * server. Everything about the carrier itself is UCarrierComponent::CanPickUp.
	 */
	UFUNCTION(BlueprintCallable, Category = "Carry")
	bool CanBePickedUpBy(const AActor* Candidate, FGameplayTag& OutFailReason) const;

	/** Server side. Called by UCarrierComponent, which owns the sequencing. */
	void SetCarrier(AActor* NewCarrier);

	/** Server side. Where the object goes when it is put back down. */
	void SetRestTransform(const FVector& InLocation, float InYaw);

	/** Fires on the server and on every client that sees the object change hands. */
	FOnCarryStateChanged OnCarryStateChanged;

protected:
	/** Which carrying rule this object follows. Only Haulable exists so far. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Carry")
	FGameplayTag CarryClass;

	/**
	 * Reach. It matches the heavy weapon's own InteractionRange so that "close enough to man
	 * the gun" and "close enough to lift it" do not disagree by a few centimetres.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "50.0", Units = "cm"))
	float PickupRange = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Carry")
	FVector CarryOffsetLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Carry")
	FRotator CarryOffsetRotation = FRotator::ZeroRotator;

	/**
	 * Radius kept clear of characters when the object lands. The gun's own collision box is
	 * 80cm half-extent, so anyone inside this is standing in the footprint and gets nudged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "0.0", Units = "cm"))
	float PutDownClearanceRadius = 130.0f;

	/** Who has it. The single replicated truth; everything else is derived from it. */
	UPROPERTY(ReplicatedUsing = OnRep_CarryState)
	TObjectPtr<AActor> Carrier = nullptr;

	/**
	 * Where the object stands when nobody is carrying it.
	 *
	 * These actors do not replicate movement -- a ground gun never moves and a deck gun rides
	 * its host -- so a put-down location has to be replicated explicitly rather than gathered
	 * as movement. Yaw alone: a gun that lands tilted is a bug, not a feature.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CarryState)
	FVector_NetQuantize100 RestLocation = FVector::ZeroVector;

	UPROPERTY(ReplicatedUsing = OnRep_CarryState)
	float RestYaw = 0.0f;

	UFUNCTION()
	void OnRep_CarryState();

private:
	/** The one place attachment and collision are decided, run the same way on every machine. */
	void ApplyCarryState();

	/** Keeps the carrier's cached pointer in step, including when the carrier changes. */
	void UpdateCarrierCache();

	/** Who ApplyCarryState last attached to, so the previous carrier's cache can be cleared. */
	TWeakObjectPtr<AActor> AppliedCarrier;

	/**
	 * Whether the last applied state was "carried". The put-down branch runs only on a real
	 * carried -> free transition: OnRep fires for a rest-location change too, and a deck gun
	 * that has never been lifted must not be detached from its ship by a stray notify.
	 */
	bool bAppliedCarried = false;
};
