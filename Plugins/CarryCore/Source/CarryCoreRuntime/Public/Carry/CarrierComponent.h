// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"

#include "CarrierComponent.generated.h"

class APawn;
class UCarryableComponent;
class USceneComponent;

/**
 * The pair of hands. One carryable at a time, held at the owner's carry point.
 *
 * Injected onto the pawn by the gameplay feature rather than built into the character, so an
 * experience that does not want carrying simply does not add it.
 *
 * Nothing here is replicated: the carried object's own UCarryableComponent replicates who
 * holds it, and this component derives its cached pointer from that on every machine. One
 * truth, one direction.
 */
UCLASS(BlueprintType, ClassGroup = (Carry), meta = (BlueprintSpawnableComponent))
class CARRYCORERUNTIME_API UCarrierComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCarrierComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "Carry")
	static UCarrierComponent* FindCarrier(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Carry")
	UCarryableComponent* GetCarried() const { return Carried.Get(); }

	UFUNCTION(BlueprintPure, Category = "Carry")
	bool IsCarrying() const { return Carried.IsValid(); }

	/**
	 * One overlap on a key press, not a scan: this runs when a player asks to lift something.
	 * Returns the nearest carryable this actor could actually take.
	 */
	UFUNCTION(BlueprintCallable, Category = "Carry")
	UCarryableComponent* FindBestCarryTarget() const;

	/** Client pre-check and server re-check call this same function. */
	UFUNCTION(BlueprintCallable, Category = "Carry")
	bool CanPickUp(const UCarryableComponent* Carryable, FGameplayTag& OutFailReason) const;

	UFUNCTION(BlueprintCallable, Category = "Carry")
	bool CanPutDown(FGameplayTag& OutFailReason) const;

	/** Server side. The client's request is a request; this is where it is granted. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Carry")
	bool ServerPickUp(UCarryableComponent* Carryable);

	/** Server side. Places the object in front of the owner and frees the hands. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Carry")
	bool ServerPutDown();

	/** Where the carried object hangs. Resolved identically on the server and every client. */
	USceneComponent* ResolveCarryAttachTarget(FName& OutSocketName) const;

	/**
	 * Relative transform for a carried object once attached: the carryable's own offset, put
	 * into the fallback pose when the character's art has no carry socket to snap to.
	 */
	FTransform GetCarryRelativeTransform(const UCarryableComponent* Carryable) const;

	/** Called by UCarryableComponent when its replicated state lands. Not for outside use. */
	void NotifyCarryStateChanged(UCarryableComponent* Carryable, bool bNowCarriedByUs);

	UFUNCTION(BlueprintPure, Category = "Carry")
	float GetSearchRadius() const { return SearchRadius; }

protected:
	/** Where the search for something to lift reaches. The carryable's own range still applies. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "50.0", Units = "cm"))
	float SearchRadius = 300.0f;

	/**
	 * Preferred socket on the owner's skeletal mesh. When the art has no such socket the
	 * object hangs off the root at CarryFallbackLocation instead, so a greybox character can
	 * carry a gun before anyone has authored a rig for it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry")
	FName CarrySocketName = TEXT("CarrySocket");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry")
	FVector CarryFallbackLocation = FVector(60.0, 0.0, 70.0);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry")
	FRotator CarryFallbackRotation = FRotator::ZeroRotator;

	/**
	 * How far in front of the carrier the object lands.
	 *
	 * Sized so the carrier is already outside the footprint before anything is pushed: a
	 * character capsule is about 42cm and the gun's collision box reaches 80cm from its
	 * centre. The nudge below is the safety net, not the plan.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "50.0", Units = "cm"))
	float PutDownForwardDistance = 240.0f;

	/** Ground search around the put-down spot, up first then down. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "0.0", Units = "cm"))
	float PutDownTraceUp = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "0.0", Units = "cm"))
	float PutDownTraceDown = 600.0f;

	/**
	 * Horizontal speed used to move a character out of the footprint the object just claimed.
	 * A nudge that reads as "step back", not a knockback: the player asked for this to happen.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "0.0", Units = "cm/s"))
	float PutDownPushAwaySpeed = 320.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Carry")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;

private:
	/** Ground under the spot in front of the owner, or that spot at foot height if there is none. */
	FVector ComputePutDownLocation(const AActor* CarriedActor) const;

	/** Pushes characters standing in the footprint out of it, once collision is back on. */
	void PushCharactersClear(const AActor* PlacedActor, const FVector& Center, float ClearanceRadius) const;

	/** Hands are only emptied through here, so a dying carrier cannot take the gun with it. */
	void PutDownOrRelease();

	TWeakObjectPtr<UCarryableComponent> Carried;
};
