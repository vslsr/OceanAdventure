// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"

#include "NavalMovementComponent.generated.h"

class UNavalHelmComponent;
class UNavalLoadComponent;
class UNavalVesselComponent;

/**
 * Turns helm intent into vessel motion, on the server only.
 *
 * It writes planar translation and yaw and nothing else, because the host's buoyancy already
 * owns Z, pitch and roll. The two never fight: buoyancy preserves yaw and XY, this preserves
 * Z and tilt, and the replicated actor transform carries the result to clients.
 *
 * Everything that degrades handling arrives from somewhere a player can attack: load bands
 * from the carry model, propulsion and rudder capability from parts, control authority from
 * the helm core. There is no separate "engine damage" number hidden here.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Naval), meta = (BlueprintSpawnableComponent))
class NAVALCORERUNTIME_API UNavalMovementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNavalMovementComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Naval|Movement")
	float GetCurrentSpeed() const { return CurrentSpeed; }

	/** Signed 0..1 against the tuned maximum; the HUD speed bar reads this. */
	UFUNCTION(BlueprintPure, Category = "Naval|Movement")
	float GetSpeedFraction() const;

	/**
	 * Scales every handling number at once.
	 *
	 * Used for the life raft, which design 8.8 puts at 65%-75% of a starting ship: enough to
	 * relocate or chase down a boarding chance, never enough to hunt with.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Movement")
	void SetSpeedScale(float NewSpeedScale);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxForwardSpeed = 900.0f;

	/** Reverse is deliberately slow: backing out of a fight should cost position. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxReverseSpeedFraction = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement", meta = (ClampMin = "1.0"))
	float Acceleration = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement", meta = (ClampMin = "1.0"))
	float BrakingDeceleration = 220.0f;

	/** Coast rate with nobody at the wheel or with the core disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement", meta = (ClampMin = "1.0"))
	float DriftDeceleration = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement", meta = (ClampMin = "1.0", Units = "deg"))
	float MaxYawRateDegrees = 26.0f;

	/**
	 * Turn authority left when every rudder is gone. Design 8.3.1: a very weak emergency
	 * correction, never a fully steerable ship.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float EmergencyRudderFraction = 0.15f;

	/** Thrust left when every propulsion module is gone -- enough to drift, not to fight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float EmergencyPropulsionFraction = 0.18f;

	/** Speed a hull with a damaged helm core can still be pushed to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement",
		meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float DamagedCoreSpeedScale = 0.8f;

	/** Turn rate scales in with speed: a stopped raft cannot pivot on the spot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinSpeedFractionForTurn = 0.12f;

	UPROPERTY(Replicated)
	float CurrentSpeed = 0.0f;

	UPROPERTY(Replicated)
	float SpeedScale = 1.0f;

private:
	void ResolvePeers();

	TWeakObjectPtr<UNavalHelmComponent> Helm;
	TWeakObjectPtr<UNavalLoadComponent> Load;
	TWeakObjectPtr<UNavalVesselComponent> Vessel;
};
