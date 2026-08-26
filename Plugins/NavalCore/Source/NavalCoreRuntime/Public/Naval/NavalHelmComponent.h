// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "GameplayTagContainer.h"
#include "Naval/NavalCoreTypes.h"
#include "Templates/SubclassOf.h"

#include "NavalHelmComponent.generated.h"

class ANavalHelmActor;
class UNavalPartComponent;
class UNavalVesselComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnNavalHelmChanged, UNavalHelmComponent* /*Helm*/);

/**
 * 舵芯总成 -- the system entity behind the thing players call 主舵台.
 *
 * The design (8.3.1) deliberately splits this into layers so no single hit anywhere on the
 * boat can end it: the wheel on deck is where a player enters control and where an enemy
 * starts a capture, the reinforced core seat below deck is the actual damage body, and the
 * rudder blades astern are separate parts again. This component owns the control and
 * ownership half; the seat's durability lives on the helm Actor's part component, and turn
 * efficiency comes from rudder parts.
 *
 * The core never manufactures mobility on its own: with propulsion gone it can only drift,
 * with rudders gone it only gets a weak emergency correction.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Naval), meta = (BlueprintSpawnableComponent))
class NAVALCORERUNTIME_API UNavalHelmComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNavalHelmComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Same-team check, seat occupancy and core state, in one place both sides can call. */
	UFUNCTION(BlueprintCallable, Category = "Naval|Helm")
	bool CanOccupy(const AActor* Candidate, FGameplayTag& OutFailReason) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Helm")
	bool TryOccupy(AActor* NewOperator);

	/** Frees the wheel. Passing null releases whoever holds it. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Helm")
	void ReleaseHelm(AActor* LeavingOperator);

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	AActor* GetOperator() const { return Operator; }

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	bool IsOperatedBy(const AActor* Candidate) const { return Candidate != nullptr && Operator == Candidate; }

	/**
	 * Server-side control write. The steering ability ships throttle and steer through GAS'
	 * target-data channel and the server applies them here; nothing on the client is trusted.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Helm")
	void SetControlIntent(AActor* Source, float InThrottle, float InSteer);

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	float GetThrottleIntent() const;

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	float GetSteerIntent() const;

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	ENavalHelmState GetHelmState() const;

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	bool AcceptsControlInput() const;

	/** 夺船: hold the wheel to enter the contest, then run the flag change to completion. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Helm")
	bool BeginCapture(AActor* Challenger);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Helm")
	void EndCapture(AActor* Challenger);

	/** Any damage the defenders land on the challenger's team pushes the progress back. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|Helm")
	void InterruptCapture(float ProgressPenalty);

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	float GetCaptureProgress() const { return CaptureProgress; }

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	int32 GetCapturingTeamId() const { return CapturingTeamId; }

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	ANavalHelmActor* GetHelmActor() const { return HelmActor; }

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	UNavalPartComponent* GetCorePart() const;

	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	UNavalVesselComponent* GetVessel() const;

	/** World-space location of the wheel; interaction range checks use it, not the hull. */
	UFUNCTION(BlueprintPure, Category = "Naval|Helm")
	FVector GetHelmWorldLocation() const;

	FOnNavalHelmChanged OnHelmChanged;

protected:
	/** Deck console spawned and attached at BeginPlay; carries the reinforced core seat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm")
	TSubclassOf<ANavalHelmActor> HelmActorClass;

	/** 初始甲板中后部的固定格. Authored in host space, never moved at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (Units = "cm"))
	FVector HelmLocalOffset = FVector(-140.0, 0.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (Units = "deg"))
	float HelmLocalYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (ClampMin = "50.0", Units = "cm"))
	float InteractionRange = 260.0f;

	/** Design 8.6: about 4 seconds of contact before the flag change even starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (ClampMin = "0.0", Units = "s"))
	float ContestEntrySeconds = 4.0f;

	/** Design 8.6: 10-12 seconds of 改旗 once contested. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (ClampMin = "1.0", Units = "s"))
	float CaptureSeconds = 11.0f;

	/**
	 * Progress lost per second after the challenger lets go. Slow on purpose: briefly
	 * stepping away to fight should not wipe the attempt, but leaving does undo it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (ClampMin = "0.0"))
	float CaptureDecayPerSecond = 0.08f;

	/**
	 * How long an operator may hold the wheel with no controller before it is freed.
	 *
	 * Same reasoning as the heavy weapon: possession hand-off and Lyra's death path both
	 * leave a pawn controllerless on purpose for a moment.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (ClampMin = "0.0", Units = "s"))
	float OrphanGraceSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm", meta = (ClampMin = "0.1", Units = "s"))
	float OrphanCheckInterval = 1.0f;

	/** Below this core durability fraction the helm reports Damaged. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Helm",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DamagedCoreFraction = 0.5f;

	UPROPERTY(Replicated)
	TObjectPtr<AActor> Operator = nullptr;

	UPROPERTY(Replicated)
	TObjectPtr<ANavalHelmActor> HelmActor = nullptr;

	UPROPERTY(Replicated)
	float CaptureProgress = 0.0f;

	UPROPERTY(Replicated)
	int32 CapturingTeamId = INDEX_NONE;

private:
	/**
	 * A disconnect kills the controller, the player state and the ability system component
	 * together, so the ability that took the wheel never reaches EndAbility and never sends
	 * its release. A ship whose wheel is locked by a departed player cannot be steered by
	 * anybody, so the helm has to free itself.
	 */
	UFUNCTION()
	void HandleOperatorDestroyed(AActor* DestroyedActor);

	/** Second line, for the case where the pawn outlives its controller instead of dying. */
	void CheckOperatorStillControlled();

	void BindOperatorDestroyed(AActor* NewOperator);
	void UnbindOperatorDestroyed();

	void SpawnHelmActor();
	void UpdateCapture(float DeltaTime);
	void CompleteCapture();
	void UpdateTickEnabled();
	void BroadcastHelmState() const;

	UPROPERTY(Transient)
	TObjectPtr<AActor> CaptureChallenger = nullptr;

	/** Seconds of uninterrupted contact so far; only past ContestEntrySeconds does 改旗 start. */
	float ContestContactSeconds = 0.0f;

	/**
	 * Replicated for presentation only -- the wheel on the console turns with them. The
	 * server writes them from validated control samples and is the only reader that moves
	 * the hull; nothing on a client is allowed to write them back.
	 */
	UPROPERTY(Replicated)
	float ThrottleIntent = 0.0f;

	UPROPERTY(Replicated)
	float SteerIntent = 0.0f;

	/** Server clock reading of the first sweep that saw the operator without a controller. */
	double OperatorLostControllerTime = 0.0;

	FTimerHandle OrphanCheckTimerHandle;
};
