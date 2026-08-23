// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"

#include "NavalProjectile.generated.h"

class UStaticMeshComponent;

/** What the firing weapon hands its shell. */
struct FNavalProjectileLaunchParams
{
	FVector InitialVelocity = FVector::ForwardVector * 3200.0f;
	float GravityZ = -40.0f;
	AActor* SourceWeapon = nullptr;
	AActor* SourceOperator = nullptr;
	int32 TeamId = INDEX_NONE;
	float MinimumRange = 0.0f;
	float MaxRange = 0.0f;
};

/**
 * A heavy weapon's shell: slow, low-arc, and loud enough to be read and dodged.
 *
 * The shallow custom gravity makes charge readable without turning the cannon into a mortar.
 * The server still applies the shared wall rule before launch and along every travelled
 * segment, so no charge can lob a shell over cover.
 *
 * The server owns the hit: it sweeps the segment travelled each frame through the shared
 * ballistic rule, so a shell is stopped by the same walls and passed by the same windows as
 * a rifle round. Clients run the same deterministic motion purely as presentation.
 */
UCLASS(BlueprintType, Blueprintable)
class NAVALCORERUNTIME_API ANavalProjectile : public AActor
{
	GENERATED_BODY()

public:
	ANavalProjectile();

	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server-side. Sets ballistic velocity, ownership and the range window, then starts the flight. */
	void LaunchProjectile(const FNavalProjectileLaunchParams& Params);

	UFUNCTION(BlueprintPure, Category = "Naval|Projectile")
	float GetSpeed() const { return FlightVelocity.Size(); }

	UFUNCTION(BlueprintPure, Category = "Naval|Projectile")
	int32 GetTeamId() const { return TeamId; }

protected:
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
	FVector_NetQuantize100 FlightVelocity = FVector::ForwardVector * 3200.0f;

	UPROPERTY(Replicated)
	float GravityZ = -40.0f;

private:
	void HandleImpact(const struct FNavalShotResult& Result);

	TWeakObjectPtr<AActor> SourceWeapon;
	TWeakObjectPtr<AActor> SourceOperator;
	FVector SpawnLocation = FVector::ZeroVector;
	float MinimumRange = 0.0f;
	float MaxRange = 0.0f;
	float ElapsedSeconds = 0.0f;
};
