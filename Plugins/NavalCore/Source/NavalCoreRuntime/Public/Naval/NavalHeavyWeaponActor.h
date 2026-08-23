// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"

#include "NavalHeavyWeaponActor.generated.h"

class ANavalProjectile;
class UBoxComponent;
class UNavalFireWindowComponent;
class UNavalPartComponent;
class USceneComponent;
class UStaticMeshComponent;

/**
 * One heavy weapon, identical whether it is staked into an island or bolted to a deck.
 *
 * Design 7.10 is specific that the difference between ground and mounted is support -- cover,
 * ammunition, steadiness -- and never whether the gun can fire at all, so there is one class
 * and one rule set. What makes it a heavy weapon rather than a big rifle is all here: it has
 * to be set up, it cannot be carried while firing, it needs a character standing at it, it
 * has a traverse arc and a minimum range, its shell is slow, and the gun itself can be shot.
 *
 * The exchange this is tuned for: setting up in the open is a readable bet, and a light
 * weapon that circles inside the minimum range or shoots the exposed gunner should win it.
 */
UCLASS(BlueprintType, Blueprintable)
class NAVALCORERUNTIME_API ANavalHeavyWeaponActor : public AActor
{
	GENERATED_BODY()

public:
	ANavalHeavyWeaponActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Starts the deploy windup and construction ramp. Server side. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|HeavyWeapon")
	void BeginDeployment(int32 InTeamId);

	UFUNCTION(BlueprintCallable, Category = "Naval|HeavyWeapon")
	bool CanOperate(const AActor* Candidate, FGameplayTag& OutFailReason) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|HeavyWeapon")
	bool TryOccupy(AActor* NewOperator);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|HeavyWeapon")
	void ReleaseOperator(AActor* LeavingOperator);

	UFUNCTION(BlueprintPure, Category = "Naval|HeavyWeapon")
	AActor* GetWeaponOperator() const { return WeaponOperator; }

	/** Server-side aim request from the operating ability; clamped to the traverse arc. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|HeavyWeapon")
	void SetDesiredAimLocation(AActor* Source, const FVector& WorldAimLocation);

	/** Immediate owning-client presentation; it never changes authoritative aim state. */
	void SetLocalPredictedAimLocation(const AActor* Source, const FVector& WorldAimLocation);

	/**
	 * Everything that can refuse a shot, in one place so the client's preview and the
	 * server's verdict cannot disagree: state, operator, reload, minimum range, and whether
	 * the shooter's own structure is in the way.
	 */
	UFUNCTION(BlueprintCallable, Category = "Naval|HeavyWeapon")
	bool CanFire(const AActor* Requester, const FVector& AimLocation, FGameplayTag& OutFailReason) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Naval|HeavyWeapon")
	bool TryFire(AActor* Requester, const FVector& AimLocation, float ChargeAlpha = 1.0f);

	/** Builds the exact low ballistic arc used by both the local preview and spawned shell. */
	UFUNCTION(BlueprintCallable, Category = "Naval|HeavyWeapon")
	bool BuildChargedTrajectory(
		const FVector& AimLocation,
		float ChargeAlpha,
		FVector& OutInitialVelocity,
		float& OutGravityZ,
		float& OutRange) const;

	UFUNCTION(BlueprintPure, Category = "Naval|HeavyWeapon")
	FVector GetMuzzleLocation() const;

	UFUNCTION(BlueprintPure, Category = "Naval|HeavyWeapon")
	FVector GetMuzzleDirection() const;

	UFUNCTION(BlueprintPure, Category = "Naval|HeavyWeapon")
	FTransform GetOperatorTransform() const;

	UFUNCTION(BlueprintPure, Category = "Naval|HeavyWeapon")
	float GetReloadSecondsRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Naval|HeavyWeapon")
	float GetMinimumRange() const { return MinimumRange; }

	UFUNCTION(BlueprintPure, Category = "Naval|HeavyWeapon")
	float GetMaxRange() const { return MaxRange; }

	UFUNCTION(BlueprintPure, Category = "Naval|HeavyWeapon")
	float GetTraverseHalfAngleDegrees() const { return TraverseHalfAngleDegrees; }

	UFUNCTION(BlueprintPure, Category = "Naval|HeavyWeapon")
	UNavalPartComponent* GetPart() const { return PartComponent; }

	UFUNCTION(BlueprintPure, Category = "Naval|HeavyWeapon")
	int32 GetTeamId() const;

	/** The friendly window this muzzle is currently paired with, if any. */
	UFUNCTION(BlueprintCallable, Category = "Naval|HeavyWeapon")
	UNavalFireWindowComponent* FindPairedWindow() const;

	/**
	 * Ground check for a field emplacement. Design 7.10: no foundation and no war banner
	 * required, but the legs still need ground that can carry the whole footprint.
	 */
	static bool IsGroundSuitable(
		const UObject* WorldContextObject,
		const FVector& Location,
		float FootprintRadius,
		float MaxSlopeDegrees,
		FGameplayTag& OutFailReason);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon")
	TSubclassOf<ANavalProjectile> ProjectileClass;

	/** Design 7.10: a gunner must be forced off the gun once an attacker gets inside this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumRange = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxRange = 14000.0f;

	/** Seconds from muzzle to the nominal end of the preview. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon", meta = (ClampMin = "0.25", Units = "s"))
	float TrajectoryFlightSeconds = 4.0f;

	/** The arc stays deliberately below ordinary walls; walls also block the horizontal fire line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxTrajectoryRise = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon",
		meta = (ClampMin = "5.0", ClampMax = "180.0", Units = "deg"))
	float TraverseHalfAngleDegrees = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon", meta = (ClampMin = "1.0", Units = "deg"))
	float TraverseSpeedDegreesPerSecond = 720.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon", meta = (ClampMin = "0.1", Units = "s"))
	float ReloadSeconds = 3.2f;

	/** Muzzle windup before the shell leaves, so the target gets a warning to move on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon", meta = (ClampMin = "0.0", Units = "s"))
	float FireWindupSeconds = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon", meta = (ClampMin = "50.0", Units = "cm"))
	float InteractionRange = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon")
	TObjectPtr<USceneComponent> SceneRoot;

	/** 自带支腿/配重底座: the reason this needs no foundation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon")
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon")
	TObjectPtr<USceneComponent> TurretPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon")
	TObjectPtr<UStaticMeshComponent> BarrelMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon")
	TObjectPtr<USceneComponent> MuzzlePoint;

	/** Character snap point, adjustable on each cannon Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon")
	TObjectPtr<USceneComponent> OperatorPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon")
	TObjectPtr<UBoxComponent> WeaponCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|HeavyWeapon")
	TObjectPtr<UNavalPartComponent> PartComponent;

	UPROPERTY(Replicated)
	TObjectPtr<AActor> WeaponOperator = nullptr;

	/** Local yaw of the barrel relative to the mount, replicated so everyone reads the arc. */
	UPROPERTY(Replicated)
	float TurretYawLocal = 0.0f;

	UPROPERTY(Replicated)
	double NextFireServerTime = 0.0;

	/** Only used when the weapon stands on the ground rather than on a vessel. */
	UPROPERTY(Replicated)
	int32 GroundTeamId = INDEX_NONE;

private:
	void HandleFireWindupElapsed();
	void SpawnProjectile();
	void BroadcastWeaponState() const;
	void HandlePartStateChanged(UNavalPartComponent* Part);

	float DesiredTurretYawLocal = 0.0f;
	float LocalPredictedTurretYawLocal = 0.0f;
	bool bHasLocalPredictedAim = false;
	FVector PendingAimLocation = FVector::ZeroVector;
	float PendingChargeAlpha = 1.0f;
	FTimerHandle FireWindupTimerHandle;
};
