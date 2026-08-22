// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"

#include "NavalProjectile.generated.h"

class UStaticMeshComponent;

/** What the firing weapon hands its shell. */
struct FNavalProjectileLaunchParams
{
	FVector Direction = FVector::ForwardVector;
	AActor* SourceWeapon = nullptr;
	AActor* SourceOperator = nullptr;
	int32 TeamId = INDEX_NONE;
	float MinimumRange = 0.0f;
	float MaxRange = 0.0f;
};

/**
 * A heavy weapon's shell: slow, straight, and loud enough to be read and dodged.
 *
 * There is no gravity and no homing, both on purpose. Design 7.7 rule 4 rules out lobbing
 * anything over a wall, and 7.10 wants a moving character to be able to sidestep a shell
 * after seeing the muzzle -- which only works if the flight is honest and readable.
 *
 * The server owns the hit: it sweeps the segment travelled each frame through the shared
 * ballistic rule, so a shell is stopped by the same walls and passed by the same windows as
 * a rifle round. Clients run the same straight-line motion purely as presentation.
 */
UCLASS(BlueprintType, Blueprintable)
class NAVALCORERUNTIME_API ANavalProjectile : public AActor
{
	GENERATED_BODY()

public:
	ANavalProjectile();

	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server-side. Sets heading, ownership and the range window, then starts the flight. */
	void LaunchProjectile(const FNavalProjectileLaunchParams& Params);

	UFUNCTION(BlueprintPure, Category = "Naval|Projectile")
	float GetSpeed() const { return Speed; }

	UFUNCTION(BlueprintPure, Category = "Naval|Projectile")
	int32 GetTeamId() const { return TeamId; }

protected:
	/** Deliberately slow. 32 m/s leaves a visible, dodgeable flight at engagement ranges. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Projectile", meta = (ClampMin = "100.0", Units = "cm/s"))
	float Speed = 3200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Projectile", meta = (ClampMin = "0.1", Units = "s"))
	float MaxLifeSeconds = 6.0f;

	/** Against walls, windows and other naval parts. The 100% baseline of design 7.10. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Projectile|Damage", meta = (ClampMin = "0.0"))
	float StructureDamage = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Projectile|Damage", meta = (ClampMin = "0.0"))
	float HullDamage = 190.0f;

	/**
	 * Handed to the gameplay layer rather than applied here. High, but the design forbids a
	 * routine heavy shell reliably one-shotting a healthy character.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Projectile|Damage", meta = (ClampMin = "0.0"))
	float CharacterDamage = 65.0f;

	/** Radius the gameplay layer applies suppression in. Walls stop it like everything else. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Projectile|Damage", meta = (ClampMin = "0.0", Units = "cm"))
	float SuppressionRadius = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Naval|Projectile")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Naval|Projectile")
	TObjectPtr<UStaticMeshComponent> ProjectileMesh;

	UPROPERTY(Replicated)
	int32 TeamId = INDEX_NONE;

	UPROPERTY(Replicated)
	FVector_NetQuantizeNormal FlightDirection = FVector::ForwardVector;

private:
	void HandleImpact(const struct FNavalShotResult& Result);

	TWeakObjectPtr<AActor> SourceWeapon;
	TWeakObjectPtr<AActor> SourceOperator;
	FVector SpawnLocation = FVector::ZeroVector;
	float MinimumRange = 0.0f;
	float MaxRange = 0.0f;
	float ElapsedSeconds = 0.0f;
};
