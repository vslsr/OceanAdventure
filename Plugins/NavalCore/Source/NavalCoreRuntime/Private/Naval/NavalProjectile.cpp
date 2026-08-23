// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalProjectile.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Naval/NavalBallistics.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalMessages.h"
#include "Naval/NavalPartComponent.h"
#include "Naval/NavalVesselComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalProjectile)

ANavalProjectile::ANavalProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// Both sides run the same straight-line motion from the replicated spawn transform, so
	// streaming a transform every frame would cost bandwidth for nothing.
	SetReplicateMovement(false);
	InitialLifeSpan = 0.0f;

	ProjectileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
	SetRootComponent(ProjectileMesh);
	// Hit detection is the segment trace in Tick, not this component. Leaving collision on
	// would let the shell stop against its own emplacement the instant it spawns.
	ProjectileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProjectileMesh->SetGenerateOverlapEvents(false);
	ProjectileMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultProjectileMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (DefaultProjectileMesh.Succeeded())
	{
		ProjectileMesh->SetStaticMesh(DefaultProjectileMesh.Object);
		ProjectileMesh->SetRelativeScale3D(FVector(0.12f));
	}
}

void ANavalProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ANavalProjectile, TeamId);
	DOREPLIFETIME(ANavalProjectile, FlightVelocity);
	DOREPLIFETIME(ANavalProjectile, GravityZ);
}

void ANavalProjectile::LaunchProjectile(const FNavalProjectileLaunchParams& Params)
{
	FlightVelocity = Params.InitialVelocity;
	GravityZ = FMath::Min(0.0f, Params.GravityZ);
	TeamId = Params.TeamId;
	SourceWeapon = Params.SourceWeapon;
	SourceOperator = Params.SourceOperator;
	MinimumRange = Params.MinimumRange;
	MaxRange = Params.MaxRange;
	SpawnLocation = GetActorLocation();
	ElapsedSeconds = 0.0f;

	SetActorRotation(FVector(FlightVelocity).Rotation());
	ForceNetUpdate();
}

void ANavalProjectile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ElapsedSeconds += DeltaTime;

	const FVector PreviousLocation = GetActorLocation();
	const FVector Velocity = FlightVelocity;
	const FVector NextLocation = PreviousLocation
		+ Velocity * DeltaTime
		+ FVector(0.0f, 0.0f, 0.5f * GravityZ * FMath::Square(DeltaTime));
	FlightVelocity.Z += GravityZ * DeltaTime;
	SetActorLocation(NextLocation, /*bSweep=*/false, nullptr, ETeleportType::None);
	if (!FlightVelocity.IsNearlyZero())
	{
		SetActorRotation(FVector(FlightVelocity).Rotation());
	}

	if (!HasAuthority())
	{
		if (ElapsedSeconds >= MaxLifeSeconds)
		{
			Destroy();
		}
		return;
	}

	FNavalShotQuery Query;
	Query.WorldContextObject = this;
	Query.Start = PreviousLocation;
	Query.End = NextLocation;
	Query.ShotInstigator = SourceOperator.Get();
	Query.TeamId = TeamId;
	Query.TraceChannel = TraceChannel;
	if (AActor* Weapon = SourceWeapon.Get())
	{
		// The gun that fired it is never its own target, at any range.
		Query.IgnoreActors.Add(Weapon);
	}
	if (AActor* Operator = SourceOperator.Get())
	{
		Query.IgnoreActors.Add(Operator);
	}

	FNavalShotResult Result;
	if (FNavalBallistics::ResolveShot(Query, Result) && Result.bHit)
	{
		HandleImpact(Result);
		return;
	}

	const bool bOutOfRange = MaxRange > 0.0f
		&& FVector::DistSquared(SpawnLocation, NextLocation) >= FMath::Square(MaxRange);
	if (bOutOfRange || ElapsedSeconds >= MaxLifeSeconds)
	{
		Destroy();
	}
}

void ANavalProjectile::HandleImpact(const FNavalShotResult& Result)
{
	AActor* HitActor = Result.Hit.GetActor();

	FNavalProjectileImpactMessage Message;
	Message.Projectile = this;
	Message.Instigator = SourceOperator.Get();
	Message.HitActor = HitActor;
	Message.ImpactLocation = Result.Hit.ImpactPoint;
	Message.ImpactNormal = Result.Hit.ImpactNormal;
	Message.SuppressionRadius = SuppressionRadius;
	Message.BlockReason = Result.Reason;
	Message.InstigatorTeamId = TeamId;

	// Structure and hull are framework truth, so they are settled here where cheats and save
	// restore can reach the same code. Characters go through GAS in the gameplay layer.
	if (UNavalPartComponent* Part = HitActor ? HitActor->FindComponentByClass<UNavalPartComponent>() : nullptr)
	{
		Message.StructureDamageApplied = Part->ApplyPartDamage(StructureDamage, SourceOperator.Get());
	}
	else if (UNavalVesselComponent* Vessel = UNavalVesselComponent::FindVessel(HitActor))
	{
		Message.StructureDamageApplied = Vessel->ApplyHullDamage(HullDamage, SourceOperator.Get());
	}
	else if (HitActor)
	{
		Message.PendingCharacterDamage = CharacterDamage;
	}

	if (const UWorld* World = GetWorld())
	{
		UGameplayMessageSubsystem::Get(World).BroadcastMessage(
			NavalGameplayTags::Message_Projectile_Impact, Message);
	}

	Destroy();
}
