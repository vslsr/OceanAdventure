// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Naval/NavalCoreTypes.h"

#include "NavalVesselComponent.generated.h"

class UNavalPartComponent;

DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnNavalVesselStateChanged, UNavalVesselComponent* /*Vessel*/, ENavalVesselState /*NewState*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnNavalVesselPartsChanged, UNavalVesselComponent* /*Vessel*/);

/**
 * The server-authoritative truth for one vessel: who owns it, how much hull is left, and
 * which of the two zero points it is standing on.
 *
 * The design is explicit that hull and helm core are different lives (8.3.1): a core at zero
 * removes active control, a hull at zero starts a 15-25s founder countdown with exactly one
 * emergency repair. Nothing here deletes the vessel or the crew's options -- a wreck is a
 * state, not a despawn.
 *
 * Independent of GAS on purpose: cheats, save restore and editor tooling drive these calls
 * directly.
 */
UCLASS(BlueprintType, ClassGroup = (Naval), meta = (BlueprintSpawnableComponent))
class NAVALCORERUNTIME_API UNavalVesselComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNavalVesselComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Nearest vessel component walking Actor -> attach parents -> owner. */
	static UNavalVesselComponent* FindVessel(const AActor* Actor);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Vessel")
	void SetTeamId(int32 NewTeamId);

	UFUNCTION(BlueprintPure, Category = "Naval|Vessel")
	int32 GetTeamId() const { return TeamId; }

	/** Returns the damage actually applied to the hull. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Vessel")
	float ApplyHullDamage(float Amount, AActor* DamageInstigator);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Vessel")
	bool RepairHull(float Amount);

	UFUNCTION(BlueprintPure, Category = "Naval|Vessel")
	float GetHullFraction() const;

	UFUNCTION(BlueprintPure, Category = "Naval|Vessel")
	float GetMaxHullDurability() const { return MaxHullDurability; }

	UFUNCTION(BlueprintPure, Category = "Naval|Vessel")
	ENavalVesselState GetVesselState() const { return VesselState; }

	UFUNCTION(BlueprintPure, Category = "Naval|Vessel")
	bool IsOperational() const { return VesselState == ENavalVesselState::Operational; }

	UFUNCTION(BlueprintPure, Category = "Naval|Vessel")
	float GetFounderSecondsRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Naval|Vessel")
	bool IsEmergencyRepairAvailable() const;

	UFUNCTION(BlueprintPure, Category = "Naval|Vessel")
	float GetEmergencyRepairSeconds() const { return EmergencyRepairSeconds; }

	/** Validation the ability runs before it starts its channel, and again before it commits. */
	UFUNCTION(BlueprintCallable, Category = "Naval|Vessel")
	bool CanBeginEmergencyRepair(const AActor* Repairer, FGameplayTag& OutFailReason) const;

	/**
	 * 一次性紧急抢修: stops the countdown and restores a slice of hull. Refused a second
	 * time in the same foundering life, which is what stops two crews trading infinite saves.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Vessel")
	bool CommitEmergencyRepair(AActor* Repairer);

	/** Enemy stabilisation: pauses the countdown so a boarding crew can take the hulk instead. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Vessel")
	bool SetFounderCountdownPaused(bool bPaused);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Vessel")
	void ForceWreck();

	/**
	 * Turns a freshly spawned hull into a 折叠救生筏.
	 *
	 * Design 8.8 is explicit that this must be clearly worse than a real ship, or abandoning
	 * a damaged ship becomes the optimal move. It scales the hull down and marks the vessel so
	 * anything that cares -- the one-ship-per-team rule, the HUD, scoring -- can tell the
	 * difference without inspecting how it was spawned.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Vessel")
	void ConfigureAsLifeRaft(float HullScale);

	UFUNCTION(BlueprintPure, Category = "Naval|Vessel")
	bool IsLifeRaft() const { return bIsLifeRaft; }

	void RegisterPart(UNavalPartComponent* Part);
	void UnregisterPart(UNavalPartComponent* Part);
	void NotifyPartChanged(UNavalPartComponent* Part);

	/**
	 * Fraction of this part type that is still working, 0..1. Movement reads propulsion and
	 * rudder from here so "all thrusters destroyed" degrades handling instead of flipping a
	 * boolean.
	 */
	UFUNCTION(BlueprintPure, Category = "Naval|Vessel")
	float GetPartCapability(ENavalPartType QueryType) const;

	const TArray<TWeakObjectPtr<UNavalPartComponent>>& GetParts() const { return Parts; }

	FOnNavalVesselStateChanged OnVesselStateChanged;
	FOnNavalVesselPartsChanged OnVesselPartsChanged;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Vessel", meta = (ClampMin = "1.0"))
	float MaxHullDurability = 1200.0f;

	/** 沉没倒计时. Design 8.7 asks for 15-25 seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Vessel", meta = (ClampMin = "1.0", Units = "s"))
	float FounderSeconds = 20.0f;

	/** Channel time the repairing player has to hold. Design 8.7 asks for 6-8 seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Vessel", meta = (ClampMin = "0.5", Units = "s"))
	float EmergencyRepairSeconds = 7.0f;

	/** Hull restored by the one emergency repair, as a fraction of maximum. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Vessel",
		meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float EmergencyRepairHullFraction = 0.12f;

	/** How close a player has to be to the hull to commit the emergency repair. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Vessel", meta = (ClampMin = "100.0", Units = "cm"))
	float EmergencyRepairRange = 900.0f;

	UPROPERTY(ReplicatedUsing = OnRep_VesselState)
	ENavalVesselState VesselState = ENavalVesselState::Operational;

	UPROPERTY(ReplicatedUsing = OnRep_HullDurability)
	float HullDurability = 1200.0f;

	UPROPERTY(Replicated)
	int32 TeamId = INDEX_NONE;

	/** Server-clock timestamp the wreck transition happens at; 0 outside foundering. */
	UPROPERTY(Replicated)
	double FounderEndServerTime = 0.0;

	UPROPERTY(Replicated)
	bool bEmergencyRepairSpent = false;

	UPROPERTY(Replicated)
	bool bIsLifeRaft = false;

	UPROPERTY(Replicated)
	bool bFounderCountdownPaused = false;

	/** Countdown left at the moment an enemy stabilised the hulk; replicated so the HUD agrees. */
	UPROPERTY(Replicated)
	float PausedFounderRemaining = 0.0f;

	UFUNCTION()
	void OnRep_VesselState();

	UFUNCTION()
	void OnRep_HullDurability();

private:
	void EnterState(ENavalVesselState NewState);
	void BeginFoundering(AActor* DamageInstigator);
	void HandleFounderElapsed();
	void ScheduleFounderTimer();
	void BroadcastVesselState() const;
	void SetHostBuoyancyEnabled(bool bEnabled);

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UNavalPartComponent>> Parts;

	FTimerHandle FounderTimerHandle;
};
