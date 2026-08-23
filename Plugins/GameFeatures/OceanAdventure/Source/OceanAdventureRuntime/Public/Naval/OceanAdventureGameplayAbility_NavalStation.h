// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "Naval/OceanAdventureNavalTargetData.h"

#include "OceanAdventureGameplayAbility_NavalStation.generated.h"

class UOceanAdventureAbilityTask_NavalControl;

/**
 * Shared behaviour for "the character walks up to a thing and operates it".
 *
 * Design 1.2 and the P0 hard rules are firm that operating the helm or a heavy weapon is a
 * state the same top-down character enters, not a mode switch into another game. So this is a
 * GameplayAbility on the same pawn, it holds a status tag that blocks the weapon abilities
 * while it runs, and leaving costs a short commitment instead of being free.
 *
 * The network shape is Lyra's ranged weapon shape: the client resolves what it wants locally,
 * ships it through GAS' target-data channel, and the server re-validates everything before it
 * writes anything. Occupancy, aim and firing are all requests.
 */
UCLASS(Abstract)
class OCEANADVENTURERUNTIME_API UOceanAdventureGameplayAbility_NavalStation : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UOceanAdventureGameplayAbility_NavalStation(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

protected:
	/** Client side. The nearest station of the subclass' kind the avatar could operate. */
	virtual AActor* FindStationInRange() const;

	/** Server side. Returns false to end the ability -- the station refused the request. */
	virtual bool ServerOccupyStation(AActor* Station);

	virtual void ServerReleaseStation(AActor* Station);

	/** Server side. A throttle/steer or aim sample arrived. */
	virtual void ServerApplyControl(AActor* Station, const FOceanAdventureNavalTargetData& Data);

	/** Client side. Fills one control sample from local input. Return false to skip the send. */
	virtual bool BuildControlSample(FOceanAdventureNavalTargetData& OutData) const;

	/** Tag held for the lifetime of the ability; blocks the abilities this station replaces. */
	virtual FGameplayTag GetStationStatusTag() const;

	/** Client side. True while the station is still worth staying at. */
	virtual bool IsStationStillValid(AActor* Station) const;

	virtual bool GetOperatorTransform(AActor* Station, FTransform& OutTransform) const;

	AActor* GetStationActor() const { return ActiveStation.Get(); }

	/** Vessel the station belongs to, when it is on one. */
	AActor* GetStationVesselActor() const;

	void SendStationRequest(const FOceanAdventureNavalTargetData& Data);
	void BroadcastStationFailure(FGameplayTag FailReason, AActor* Station) const;

	/** How far the avatar may be from a station to take it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "50.0", Units = "cm"))
	float StationSearchRadius = 300.0f;

	/** Design 7.10: 0.35-0.6s from leaving the gun to a light weapon being usable again. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "0.0", Units = "s"))
	float StationExitLockSeconds = 0.45f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OceanAdventure|Naval", meta = (ClampMin = "0.01", Units = "s"))
	float ControlSampleInterval = 0.05f;

	/**
	 * Pins the character to the station and stops it walking. Attachment rather than a
	 * teleport-per-frame is what makes the operator ride a moving deck for free.
	 */
	void EnterStationPresentation(AActor* Station);
	void LeaveStationPresentation();

private:
	void OnTargetDataReadyCallback(const FGameplayAbilityTargetDataHandle& InData, FGameplayTag ApplicationTag);
	void HandleControlSample(float DeltaTime);
	void StartLeaveInputWatch();
	void ApplyExitLock();

	UFUNCTION()
	void OnLeaveInputPressed(float TimeWaited);

	TWeakObjectPtr<AActor> ActiveStation;

	UPROPERTY()
	TObjectPtr<UOceanAdventureAbilityTask_NavalControl> ControlTask;

	FDelegateHandle OnTargetDataReadyHandle;
	bool bStationEntered = false;
};
