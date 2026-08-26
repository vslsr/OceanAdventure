// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"

#include "NavalMovementComponent.generated.h"

class UNavalHelmComponent;
class UNavalLoadComponent;
class UNavalVesselComponent;

/**
 * Turns helm intent into vessel motion on the server, and keeps that motion smooth on clients.
 *
 * The server writes planar translation and yaw and nothing else, because the host's buoyancy
 * already owns Z, pitch and roll. The two never fight: buoyancy preserves yaw and XY, this
 * preserves Z and tilt.
 *
 * Clients do not simulate any of it. They receive the authoritative pose from this component
 * rather than from AActor's movement replication, carry it forward by the velocity it was made
 * with, and ease out whatever is left over -- so a hull moves continuously instead of stepping
 * 30 times a second. That is presentation only: nothing a client computes here is ever sent
 * back, and a correction is always the server's pose winning.
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

	/** Lateral water drag makes the velocity direction settle toward the hull heading. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement", meta = (ClampMin = "1.0"))
	float LateralDrag = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement", meta = (ClampMin = "1.0", Units = "deg/s"))
	float MaxYawRateDegrees = 26.0f;

	/** AD is a torque input: angular velocity ramps up instead of snapping to a yaw rate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement", meta = (ClampMin = "1.0"))
	float YawAccelerationDegrees = 90.0f;

	/** Angular drag returns the hull to a stable heading when AD is released. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement", meta = (ClampMin = "0.0"))
	float YawDamping = 5.0f;

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

	/** Retained as a tuning floor for content that wants a reduced stationary torque response. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinSpeedFractionForTurn = 0.12f;

	UPROPERTY(Replicated)
	float CurrentSpeed = 0.0f;

	UPROPERTY(Replicated)
	float SpeedScale = 1.0f;

	/**
	 * How hard a client pulls the hull onto the authoritative pose, as an interpolation speed.
	 *
	 * This is the whole trade: too low and the boat lags behind what the server says during a
	 * turn, too high and it inherits the 30Hz staircase this exists to remove. It only ever
	 * corrects the residual after dead reckoning, so it can be gentle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement|Client", meta = (ClampMin = "0.1"))
	float ClientCorrectionSpeed = 12.0f;

	/**
	 * Past this much error a client stops correcting and teleports.
	 *
	 * A spawn, a respawn or a connection coming back leaves the hull an arbitrary distance from
	 * where it belongs, and sliding a boat -- with people standing on it -- across that gap
	 * would be far worse than a jump.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement|Client", meta = (ClampMin = "50.0", Units = "cm"))
	float ClientSnapDistance = 800.0f;

	/** Ceiling on dead reckoning, so a stalled connection coasts rather than sails away. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement|Client", meta = (ClampMin = "0.0", Units = "s"))
	float ClientMaxExtrapolationSeconds = 0.4f;

	/**
	 * The authoritative pose, published by this component instead of by the engine's movement
	 * replication.
	 *
	 * AActor's own replicated movement snaps a simulated proxy onto each update, which at 30Hz
	 * is a visible staircase -- and every passenger standing on the deck rides that staircase.
	 * Owning the pose here is what lets a client smooth it instead.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ServerPose)
	FVector_NetQuantize100 ServerLocation = FVector::ZeroVector;

	UPROPERTY(ReplicatedUsing = OnRep_ServerPose)
	FRotator ServerRotation = FRotator::ZeroRotator;

	/** Sent so a client can carry the hull forward between updates rather than trail behind. */
	UPROPERTY(Replicated)
	FVector_NetQuantize100 ServerPlanarVelocity = FVector::ZeroVector;

	UPROPERTY(Replicated)
	float ServerYawRateDegrees = 0.0f;

private:
	void ResolvePeers();

	/**
	 * A hull whose root component is Static silently refuses every move.
	 *
	 * SetActorLocationAndRotation is dropped, the only symptom is a per-frame PIE warning, and
	 * everything else reads correct: the helm resolves, the throttle arrives, the boat sits
	 * still. A stale Mobility saved on a Blueprint or on a placed instance is enough to cause
	 * it, and no amount of fixing the C++ default reaches those. So the component whose job is
	 * to move the hull insists the hull can be moved.
	 */
	void EnsureOwnerCanMove();

	/** Server: integrate the helm's intent into a new hull pose. */
	void TickServerMovement(float DeltaTime);

	/** Server: hand the clients the pose and the motion it was made with. */
	void PublishServerPose();

	/** Client: dead reckon from the last pose and ease the residual out. */
	void TickClientSmoothing(float DeltaTime);

	UFUNCTION()
	void OnRep_ServerPose();

	TWeakObjectPtr<UNavalHelmComponent> Helm;
	TWeakObjectPtr<UNavalLoadComponent> Load;
	TWeakObjectPtr<UNavalVesselComponent> Vessel;

	/** Server-only world-space planar velocity. What clients see is ServerLocation, smoothed. */
	FVector PlanarVelocity = FVector::ZeroVector;
	float YawRateDegrees = 0.0f;

	/** Client-side: when the last pose landed, and whether one ever has. */
	double LastServerPoseTime = 0.0;
	bool bHasServerPose = false;
};
