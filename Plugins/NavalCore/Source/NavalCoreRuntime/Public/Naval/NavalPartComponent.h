// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Naval/NavalCoreTypes.h"

#include "NavalPartComponent.generated.h"

class UNavalVesselComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnNavalPartStateChanged, UNavalPartComponent* /*Part*/);

/**
 * Durability and the construction window for one damageable naval part.
 *
 * Every rule the design attaches to "a thing on a boat that can be shot" lives here so a
 * pontoon, a wall, a rudder and a cannon all read the same way: a deployment window with no
 * collision, a construction window that blocks shots but provides nothing, then function.
 *
 * Server authoritative. Clients receive the state and the durability and only present it.
 */
UCLASS(BlueprintType, ClassGroup = (Naval), meta = (BlueprintSpawnableComponent))
class NAVALCORERUNTIME_API UNavalPartComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNavalPartComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Runs the deploy windup then the construction ramp. Call it on the server right after
	 * a piece is placed; parts that ship with the hull skip it and start operational.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Part")
	void BeginConstruction();

	/** Skips both windows. Used for the T0 raft's own parts and for save-game restore. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Part")
	void SetOperationalImmediately();

	/** Returns the damage actually applied. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Part")
	float ApplyPartDamage(float Amount, AActor* DamageInstigator);

	/** Refused inside the post-damage repair lockout, which is what stops infinite wall healing. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Part")
	bool RepairPart(float Amount);

	UFUNCTION(BlueprintPure, Category = "Naval|Part")
	ENavalPartType GetPartType() const { return PartType; }

	UFUNCTION(BlueprintPure, Category = "Naval|Part")
	ENavalPartState GetPartState() const { return PartState; }

	UFUNCTION(BlueprintPure, Category = "Naval|Part")
	float GetDurabilityFraction() const;

	UFUNCTION(BlueprintPure, Category = "Naval|Part")
	float GetDurability() const { return Durability; }

	/** Only an operational part contributes tonnage capacity, thrust, steering or fire. */
	UFUNCTION(BlueprintPure, Category = "Naval|Part")
	bool IsFunctional() const { return PartState == ENavalPartState::Operational; }

	/**
	 * True once the part has physical presence. Deliberately true during construction: the
	 * design wants a half-built wall to block shots, just with low durability, so that
	 * dropping a wall in front of an inbound shell is a real but fragile play.
	 */
	UFUNCTION(BlueprintPure, Category = "Naval|Part")
	bool IsPhysicallyPresent() const;

	UFUNCTION(BlueprintPure, Category = "Naval|Part")
	float GetConstructionProgress() const;

	UFUNCTION(BlueprintPure, Category = "Naval|Part")
	UNavalVesselComponent* GetOwningVessel() const;

	/** Configuration applied by a build piece's naval fragment at spawn time. */
	void ConfigureFromFragment(
		ENavalPartType InPartType,
		float InMaxDurability,
		float InDeploySeconds,
		float InConstructionSeconds);

	float GetTonnageContribution() const { return TonnageContribution; }
	float GetBuoyancyContribution() const { return IsFunctional() ? BuoyancyContribution : 0.0f; }
	float GetThrustContribution() const { return IsFunctional() ? ThrustContribution : 0.0f; }
	float GetSteeringContribution() const { return IsFunctional() ? SteeringContribution : 0.0f; }

	void SetLoadContribution(float InTonnage, float InBuoyancy, float InThrust, float InSteering);

	FOnNavalPartStateChanged OnPartStateChanged;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Part")
	ENavalPartType PartType = ENavalPartType::Structure;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Part", meta = (ClampMin = "1.0"))
	float MaxDurability = 100.0f;

	/** 部署前摇: no collision, no blocking, no function, fully refundable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Part", meta = (ClampMin = "0.0", Units = "s"))
	float DeploySeconds = 0.8f;

	/** 实体施工 window that follows the windup. Zero means the part finishes instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Part", meta = (ClampMin = "0.0", Units = "s"))
	float ConstructionSeconds = 1.7f;

	/** Durability a part starts construction with, as a fraction of MaxDurability. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Part",
		meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float ConstructionStartFraction = 0.25f;

	/** 维修锁定 after taking damage. Design 7.8 asks for 4-6 seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Part", meta = (ClampMin = "0.0", Units = "s"))
	float RepairLockoutSeconds = 5.0f;

	/** Whether reaching zero durability destroys the actor or leaves it as a hulk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Part")
	bool bDestroyOwnerWhenDisabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Load", meta = (ClampMin = "0.0"))
	float TonnageContribution = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Load", meta = (ClampMin = "0.0"))
	float BuoyancyContribution = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Load", meta = (ClampMin = "0.0"))
	float ThrustContribution = 0.0f;

	/** Relative steering authority; only rudders and side thrusters set it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Load", meta = (ClampMin = "0.0"))
	float SteeringContribution = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_PartState)
	ENavalPartState PartState = ENavalPartState::Operational;

	UPROPERTY(ReplicatedUsing = OnRep_Durability)
	float Durability = 100.0f;

	UFUNCTION()
	void OnRep_PartState();

	UFUNCTION()
	void OnRep_Durability();

private:
	void EnterState(ENavalPartState NewState);
	void FinishDeployWindup();
	void FinishConstruction();
	void BroadcastPartState() const;
	void ResolveVessel();

	TWeakObjectPtr<UNavalVesselComponent> OwningVessel;
	FTimerHandle StateTimerHandle;

	/** Server clock the construction ramp is measured from. */
	double ConstructionStartServerTime = 0.0;

	/** Far enough in the past that a freshly spawned part is never repair-locked. */
	double LastDamageServerTime = -1000.0;

	/**
	 * Damage suffered while the ramp was still climbing. Kept separate so completing
	 * construction restores the schedule, not the holes an attacker already punched.
	 */
	float ConstructionDamageTaken = 0.0f;
};
