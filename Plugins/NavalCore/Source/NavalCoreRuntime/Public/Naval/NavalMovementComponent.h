// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"

#include "NavalMovementComponent.generated.h"

class UNavalHelmComponent;
class UNavalLoadComponent;
class UNavalVesselComponent;

/** One coherent server movement sample consumed only by client-side presentation smoothing. */
USTRUCT()
struct NAVALCORERUNTIME_API FNavalReplicatedPose
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize100 Location = FVector::ZeroVector;

	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY()
	FVector_NetQuantize100 PlanarVelocity = FVector::ZeroVector;

	UPROPERTY()
	float YawRateDegrees = 0.0f;
};

/**
 * Turns helm intent into authoritative vessel motion and smooths replicated motion on clients.
 *
 * The server writes planar translation and yaw and nothing else, because the host's buoyancy
 * already owns Z, pitch and roll. Clients do not simulate either system. This component sends
 * a coherent pose/velocity sample and eases the client Actor toward it, avoiding the 30 Hz
 * staircase caused by AActor::ReplicatedMovement without adding input prediction.
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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

	/** Half-life of the client-only location correction toward the latest server sample. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement|Smoothing",
		meta = (ClampMin = "0.001", Units = "s"))
	float ClientLocationSmoothingHalfLife = 0.05f;

	/** Client-only rotation chase, kept separate because buoyancy also changes pitch and roll. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement|Smoothing",
		meta = (ClampMin = "0.001", Units = "s"))
	float ClientRotationSmoothingHalfLife = 0.06f;

	/** Large corrections and deliberate teleports bypass interpolation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement|Smoothing",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ClientSnapDistance = 500.0f;

	/** Large replicated rotation changes bypass interpolation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement|Smoothing",
		meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float ClientSnapAngleDegrees = 45.0f;

	/** Short dead-reckoning window; prevents steady lag without turning this into prediction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Movement|Smoothing",
		meta = (ClampMin = "0.0", ClampMax = "0.5", Units = "s"))
	float ClientMaxExtrapolationSeconds = 0.2f;

	UPROPERTY(Replicated)
	float CurrentSpeed = 0.0f;

	UPROPERTY(Replicated)
	float SpeedScale = 1.0f;

	/** Replaces AActor::ReplicatedMovement while this component owns vessel movement. */
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedPose)
	FNavalReplicatedPose ReplicatedPose;

private:
	void ResolvePeers();

	/** Repairs stale Blueprint/instance mobility that would silently discard every move. */
	void EnsureOwnerCanMove();
	void TickAuthorityMovement(float DeltaTime);
	void PublishAuthorityPose();
	void TickClientInterpolation(AActor& OwnerActor, float DeltaTime);

	UFUNCTION()
	void OnRep_ReplicatedPose();

	TWeakObjectPtr<UNavalHelmComponent> Helm;
	TWeakObjectPtr<UNavalLoadComponent> Load;
	TWeakObjectPtr<UNavalVesselComponent> Vessel;

	/** Server-only world-space planar velocity. */
	FVector PlanarVelocity = FVector::ZeroVector;
	float YawRateDegrees = 0.0f;

	double LastReplicatedPoseTime = 0.0;
	bool bHasReplicatedPose = false;
	bool bPreviousOwnerReplicateMovement = true;
	bool bChangedOwnerReplicateMovement = false;
};
