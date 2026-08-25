// Copyright Epic Games, Inc. All Rights Reserved.

#include "Naval/NavalHeavyWeaponActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Naval/NavalBallistics.h"
#include "Naval/NavalCoreTypes.h"
#include "Naval/NavalFireWindowComponent.h"
#include "Naval/NavalGameplayTags.h"
#include "Naval/NavalMessages.h"
#include "Naval/NavalPartComponent.h"
#include "Naval/NavalProjectile.h"
#include "Naval/NavalTeamStatics.h"
#include "Naval/NavalTimeStatics.h"
#include "Naval/NavalVesselComponent.h"
#include "NavalCoreRuntimeModule.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavalHeavyWeaponActor)

ANavalHeavyWeaponActor::ANavalHeavyWeaponActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// Ground guns never move and deck guns ride their host's attachment, so movement
	// replication would only fight the attachment.
	SetReplicateMovement(false);
	NetDormancy = DORM_Never;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(SceneRoot);
	BaseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BaseMesh->SetCanEverAffectNavigation(false);

	TurretPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TurretPivot"));
	TurretPivot->SetupAttachment(SceneRoot);

	BarrelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrelMesh"));
	BarrelMesh->SetupAttachment(TurretPivot);
	BarrelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BarrelMesh->SetCanEverAffectNavigation(false);

	MuzzlePoint = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzlePoint"));
	MuzzlePoint->SetupAttachment(TurretPivot);
	MuzzlePoint->SetRelativeLocation(FVector(160.0, 0.0, 70.0));

	OperatorPoint = CreateDefaultSubobject<USceneComponent>(TEXT("OperatorPoint"));
	OperatorPoint->SetupAttachment(SceneRoot);
	OperatorPoint->SetRelativeLocation(FVector(-110.0f, 0.0f, 90.0f));

	// One collision body for the whole gun: the design wants the weapon itself shot apart,
	// not its foundation, so there is nothing else to aim at.
	WeaponCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("WeaponCollision"));
	WeaponCollision->SetupAttachment(SceneRoot);
	WeaponCollision->SetBoxExtent(FVector(80.0, 80.0, 70.0));
	WeaponCollision->SetRelativeLocation(FVector(0.0, 0.0, 70.0));
	WeaponCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WeaponCollision->SetCollisionObjectType(ECC_WorldDynamic);
	WeaponCollision->SetCollisionResponseToAllChannels(ECR_Block);
	WeaponCollision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	WeaponCollision->SetCanEverAffectNavigation(false);

	// A working default so a greybox gun fires without an authored projectile asset; a real
	// shell Blueprint replaces it per feature.
	ProjectileClass = ANavalProjectile::StaticClass();

	PartComponent = CreateDefaultSubobject<UNavalPartComponent>(TEXT("PartComponent"));
	// 2.5-4s to set up a field gun, per design 7.8's field heavy weapon band.
	PartComponent->ConfigureFromFragment(
		ENavalPartType::HeavyWeapon,
		/*MaxDurability=*/320.0f,
		/*DeploySeconds=*/0.9f,
		/*ConstructionSeconds=*/2.4f);
}

void ANavalHeavyWeaponActor::BeginPlay()
{
	Super::BeginPlay();

	if (PartComponent)
	{
		PartComponent->OnPartStateChanged.AddUObject(this, &ANavalHeavyWeaponActor::HandlePartStateChanged);
	}

	if (HasAuthority() && OrphanCheckInterval > 0.0f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				OrphanCheckTimerHandle,
				this,
				&ANavalHeavyWeaponActor::CheckOperatorStillControlled,
				OrphanCheckInterval,
				/*bLoop=*/true);
		}
	}

	BroadcastWeaponState();
}

void ANavalHeavyWeaponActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindOperatorDestroyed();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(OrphanCheckTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void ANavalHeavyWeaponActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ANavalHeavyWeaponActor, WeaponOperator);
	DOREPLIFETIME(ANavalHeavyWeaponActor, TurretYawLocal);
	DOREPLIFETIME(ANavalHeavyWeaponActor, NextFireServerTime);
	DOREPLIFETIME(ANavalHeavyWeaponActor, GroundTeamId);
}

void ANavalHeavyWeaponActor::BeginDeployment(int32 InTeamId)
{
	if (!HasAuthority())
	{
		return;
	}

	GroundTeamId = InTeamId;
	if (PartComponent)
	{
		PartComponent->BeginConstruction();
	}

	// The setup itself is the warning. An enemy who sees this has time to close, break the
	// half-built gun, or simply leave the arc.
	FNavalAlertMessage Alert;
	Alert.Vessel = this;
	Alert.AlertTag = NavalGameplayTags::Alert_HeavyWeaponDeploying;
	Alert.WorldLocation = GetActorLocation();
	Alert.TeamId = InTeamId;
	if (const UWorld* World = GetWorld())
	{
		UGameplayMessageSubsystem::Get(World).BroadcastMessage(NavalGameplayTags::Message_Vessel_Alert, Alert);
	}
	ForceNetUpdate();
}

int32 ANavalHeavyWeaponActor::GetTeamId() const
{
	// A deck gun belongs to whoever owns the ship, which is what makes a captured raft's guns
	// change hands in one authoritative write instead of piece by piece.
	if (const UNavalVesselComponent* Vessel = UNavalVesselComponent::FindVessel(this))
	{
		return Vessel->GetTeamId();
	}
	return GroundTeamId;
}

bool ANavalHeavyWeaponActor::CanOperate(const AActor* Candidate, FGameplayTag& OutFailReason) const
{
	if (!Candidate)
	{
		OutFailReason = NavalGameplayTags::Fail_WrongTeam;
		return false;
	}
	if (!PartComponent || PartComponent->GetPartState() == ENavalPartState::Deploying)
	{
		OutFailReason = NavalGameplayTags::Fail_UnderConstruction;
		return false;
	}
	if (!PartComponent->IsFunctional())
	{
		// A gun still being built is a target, not a weapon.
		OutFailReason = PartComponent->GetPartState() == ENavalPartState::UnderConstruction
			? NavalGameplayTags::Fail_UnderConstruction
			: NavalGameplayTags::Fail_NotOperational;
		return false;
	}

	const int32 WeaponTeam = GetTeamId();
	if (NavalTeam::IsValidTeam(WeaponTeam) && NavalTeam::GetTeamId(Candidate) != WeaponTeam)
	{
		OutFailReason = NavalGameplayTags::Fail_WrongTeam;
		return false;
	}
	if (WeaponOperator != nullptr && WeaponOperator != Candidate)
	{
		OutFailReason = NavalGameplayTags::Fail_SeatOccupied;
		return false;
	}

	const double DistanceSquared = FVector::DistSquared(Candidate->GetActorLocation(), GetActorLocation());
	if (DistanceSquared > FMath::Square(static_cast<double>(InteractionRange)))
	{
		OutFailReason = NavalGameplayTags::Fail_TooFar;
		return false;
	}

	return true;
}

bool ANavalHeavyWeaponActor::TryOccupy(AActor* NewOperator)
{
	if (!HasAuthority())
	{
		return false;
	}

	FGameplayTag FailReason;
	if (!CanOperate(NewOperator, FailReason))
	{
		return false;
	}

	UnbindOperatorDestroyed();
	WeaponOperator = NewOperator;
	OperatorLostControllerTime = 0.0;
	BindOperatorDestroyed(NewOperator);
	ForceNetUpdate();
	BroadcastWeaponState();
	return true;
}

void ANavalHeavyWeaponActor::ReleaseOperator(AActor* LeavingOperator)
{
	if (!HasAuthority())
	{
		return;
	}
	if (LeavingOperator != nullptr && WeaponOperator != LeavingOperator)
	{
		return;
	}

	UE_LOG(
		LogNavalCore,
		Display,
		TEXT("[HeavyWeapon] Operator released weapon=%s leaving=%s previous_operator=%s"),
		*GetName(),
		*GetNameSafe(LeavingOperator),
		*GetNameSafe(WeaponOperator.Get()));

	UnbindOperatorDestroyed();
	WeaponOperator = nullptr;
	OperatorLostControllerTime = 0.0;
	// A shot already in its windup is not cancelled by stepping away; committing to the shot
	// is part of what makes the gun risky to use.
	ForceNetUpdate();
	BroadcastWeaponState();
}

void ANavalHeavyWeaponActor::BindOperatorDestroyed(AActor* NewOperator)
{
	if (HasAuthority() && IsValid(NewOperator))
	{
		NewOperator->OnDestroyed.AddDynamic(this, &ANavalHeavyWeaponActor::HandleOperatorDestroyed);
	}
}

void ANavalHeavyWeaponActor::UnbindOperatorDestroyed()
{
	AActor* CurrentOperator = WeaponOperator.Get();
	if (IsValid(CurrentOperator))
	{
		CurrentOperator->OnDestroyed.RemoveDynamic(this, &ANavalHeavyWeaponActor::HandleOperatorDestroyed);
	}
}

void ANavalHeavyWeaponActor::HandleOperatorDestroyed(AActor* DestroyedActor)
{
	// Deliberately the same exit as pressing the interact key again: ReleaseOperator is what
	// carries ForceNetUpdate and the state broadcast, so the HUD, audio and anything else
	// subscribed to the weapon message learns the seat is free without a second code path.
	UE_LOG(
		LogNavalCore,
		Display,
		TEXT("[HeavyWeapon] Operator destroyed weapon=%s operator=%s"),
		*GetNameSafe(this),
		*GetNameSafe(DestroyedActor));
	ReleaseOperator(DestroyedActor);
}

void ANavalHeavyWeaponActor::CheckOperatorStillControlled()
{
	AActor* CurrentOperator = WeaponOperator.Get();
	if (!HasAuthority() || CurrentOperator == nullptr)
	{
		OperatorLostControllerTime = 0.0;
		return;
	}

	// Torn down but not yet collected: there is no hand-off to wait for.
	if (!IsValid(CurrentOperator))
	{
		ReleaseOperator(CurrentOperator);
		return;
	}

	// Only a pawn can lose a controller. Anything else holding the seat is released by its
	// own destruction or by an explicit call.
	const APawn* OperatorPawn = Cast<APawn>(CurrentOperator);
	if (!OperatorPawn || OperatorPawn->GetController() != nullptr)
	{
		OperatorLostControllerTime = 0.0;
		return;
	}

	const double Now = NavalTime::GetNetworkTimeSeconds(this);
	if (OperatorLostControllerTime <= 0.0)
	{
		OperatorLostControllerTime = Now;
		return;
	}
	if (Now - OperatorLostControllerTime >= static_cast<double>(OrphanGraceSeconds))
	{
		UE_LOG(
			LogNavalCore,
			Display,
			TEXT("[HeavyWeapon] Operator lost its controller, freeing seat weapon=%s operator=%s"),
			*GetNameSafe(this),
			*GetNameSafe(CurrentOperator));
		ReleaseOperator(CurrentOperator);
	}
}

void ANavalHeavyWeaponActor::SetDesiredAimLocation(AActor* Source, const FVector& WorldAimLocation)
{
	if (!HasAuthority() || Source == nullptr || WeaponOperator != Source)
	{
		return;
	}

	PendingAimLocation = WorldAimLocation;

	const FVector LocalAim = GetActorTransform().InverseTransformPosition(WorldAimLocation);
	const float RawYaw = FMath::RadiansToDegrees(FMath::Atan2(LocalAim.Y, LocalAim.X));
	// Clamped, not wrapped: the operator has to physically rotate the mount or give up the
	// angle, which is what stops a gun covering its own blind side.
	DesiredTurretYawLocal = FMath::Clamp(RawYaw, -TraverseHalfAngleDegrees, TraverseHalfAngleDegrees);
}

void ANavalHeavyWeaponActor::SetLocalPredictedAimLocation(
	const AActor* Source, const FVector& WorldAimLocation)
{
	if (HasAuthority() || !Source || Source->GetAttachParentActor() != this)
	{
		return;
	}

	const FVector LocalAim = GetActorTransform().InverseTransformPosition(WorldAimLocation);
	const float RawYaw = FMath::RadiansToDegrees(FMath::Atan2(LocalAim.Y, LocalAim.X));
	LocalPredictedTurretYawLocal = FMath::Clamp(
		RawYaw, -TraverseHalfAngleDegrees, TraverseHalfAngleDegrees);
	bHasLocalPredictedAim = true;
}

void ANavalHeavyWeaponActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (HasAuthority() && !FMath::IsNearlyEqual(TurretYawLocal, DesiredTurretYawLocal, 0.05f))
	{
		// Scalar interpolation takes the long route when the target crosses +180/-180.
		// Advance through the normalized angular delta so a full-traverse gun always turns
		// along the shortest arc under the cursor.
		const float DeltaYaw = FMath::FindDeltaAngleDegrees(TurretYawLocal, DesiredTurretYawLocal);
		const float MaxStep = TraverseSpeedDegreesPerSecond * DeltaTime;
		TurretYawLocal = FMath::UnwindDegrees(
			TurretYawLocal + FMath::Clamp(DeltaYaw, -MaxStep, MaxStep));
	}

	if (TurretPivot)
	{
		const float DisplayYaw = !HasAuthority() && bHasLocalPredictedAim
			? LocalPredictedTurretYawLocal
			: TurretYawLocal;
		TurretPivot->SetRelativeRotation(FRotator(0.0f, DisplayYaw, 0.0f));
	}
}

FVector ANavalHeavyWeaponActor::GetMuzzleLocation() const
{
	return MuzzlePoint ? MuzzlePoint->GetComponentLocation() : GetActorLocation();
}

FVector ANavalHeavyWeaponActor::GetMuzzleDirection() const
{
	return TurretPivot ? TurretPivot->GetForwardVector() : GetActorForwardVector();
}

float ANavalHeavyWeaponActor::GetReloadSecondsRemaining() const
{
	const double Now = NavalTime::GetNetworkTimeSeconds(this);
	return static_cast<float>(FMath::Max(0.0, NextFireServerTime - Now));
}

UNavalFireWindowComponent* ANavalHeavyWeaponActor::FindPairedWindow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Whatever the muzzle is pointing at first: if that happens to be a friendly window in
	// its authorised zone, the pair is formed. Nothing further away can be "borrowed".
	const FVector MuzzleLocation = GetMuzzleLocation();
	const FVector MuzzleDirection = GetMuzzleDirection();

	FCollisionQueryParams TraceParams(FName(TEXT("NavalWindowPairing")), /*bTraceComplex=*/false);
	TraceParams.AddIgnoredActor(this);
	if (const AActor* Operator = WeaponOperator)
	{
		TraceParams.AddIgnoredActor(Operator);
	}

	FHitResult Hit;
	const FVector TraceEnd = MuzzleLocation + MuzzleDirection * 400.0;
	if (!World->LineTraceSingleByChannel(Hit, MuzzleLocation, TraceEnd, TraceChannel, TraceParams))
	{
		return nullptr;
	}

	const AActor* HitActor = Hit.GetActor();
	UNavalFireWindowComponent* Window = HitActor ? HitActor->FindComponentByClass<UNavalFireWindowComponent>() : nullptr;
	if (Window && Window->IsMuzzlePaired(MuzzleLocation, MuzzleDirection, GetTeamId()))
	{
		return Window;
	}
	return nullptr;
}

bool ANavalHeavyWeaponActor::CanFire(
	const AActor* Requester, const FVector& AimLocation, FGameplayTag& OutFailReason) const
{
	if (!PartComponent || !PartComponent->IsFunctional())
	{
		OutFailReason = PartComponent && PartComponent->GetPartState() == ENavalPartState::UnderConstruction
			? NavalGameplayTags::Fail_UnderConstruction
			: NavalGameplayTags::Fail_NotOperational;
		return false;
	}
	if (Requester == nullptr || WeaponOperator != Requester)
	{
		OutFailReason = NavalGameplayTags::Fail_SeatOccupied;
		return false;
	}
	if (GetReloadSecondsRemaining() > 0.0f)
	{
		OutFailReason = NavalGameplayTags::Fail_Reloading;
		return false;
	}

	const FVector MuzzleLocation = GetMuzzleLocation();
	if (FVector::DistSquared(MuzzleLocation, AimLocation) < FMath::Square(static_cast<double>(MinimumRange)))
	{
		OutFailReason = NavalGameplayTags::Fail_MinimumRange;
		return false;
	}

	// Muzzle blocking, including by the shooter's own wall. A friendly one-way window in the
	// way is not a block: that is the pairing the design wants players to build towards.
	FNavalShotQuery Query;
	Query.WorldContextObject = this;
	Query.Start = MuzzleLocation;
	Query.End = AimLocation;
	Query.TeamId = GetTeamId();
	Query.TraceChannel = TraceChannel;
	Query.MinimumRange = 0.0f;
	Query.IgnoreActors.Add(const_cast<ANavalHeavyWeaponActor*>(this));
	if (WeaponOperator)
	{
		Query.IgnoreActors.Add(WeaponOperator);
	}

	ENavalShotBlockReason BlockReason = ENavalShotBlockReason::None;
	FVector BlockLocation = FVector::ZeroVector;
	if (!FNavalBallistics::IsFireLineClear(Query, BlockReason, BlockLocation))
	{
		OutFailReason = NavalGameplayTags::Fail_MuzzleBlocked;
		return false;
	}

	return true;
}

bool ANavalHeavyWeaponActor::BuildChargedTrajectory(
	const FVector& AimLocation,
	float ChargeAlpha,
	FVector& OutInitialVelocity,
	float& OutGravityZ,
	float& OutRange) const
{
	const FVector MuzzleLocation = GetMuzzleLocation();
	FVector PlanarDirection = GetMuzzleDirection().GetSafeNormal2D();
	if (PlanarDirection.IsNearlyZero())
	{
		PlanarDirection = GetActorForwardVector().GetSafeNormal2D();
	}

	// An emplacement configured with MinimumRange above MaxRange would walk the impact point
	// inwards as the gunner charges, which reads as the charge doing the opposite of its job.
	const float NearRange = FMath::Min(MinimumRange, MaxRange);
	const float FarRange = FMath::Max(MinimumRange, MaxRange);
	if (MinimumRange > MaxRange)
	{
		UE_LOG(
			LogNavalCore,
			Warning,
			TEXT("[HeavyWeapon] %s has MinimumRange=%.0f above MaxRange=%.0f; charging would pull the shot in. Swapping for this shot."),
			*GetName(),
			MinimumRange,
			MaxRange);
	}

	OutRange = FMath::Lerp(NearRange, FarRange, FMath::Clamp(ChargeAlpha, 0.0f, 1.0f));

	// One ballistic family for every charge level: the launch pitch is fixed, so a short shot
	// is a small arc and a full charge is that same arc scaled up. Apex = Range * tan(Pitch)/4,
	// which makes MaxTrajectoryRise the apex reached at full charge and lets every shorter
	// shot rise proportionally less.
	//
	// The previous form held both the apex and the flight time constant across the whole
	// charge range, so a minimum-charge shell lobbed to the same height as a full-charge one
	// and every shot hung in the air for TrajectoryFlightSeconds regardless of distance.
	const float ApexAtFullCharge = FMath::Max(0.0f, MaxTrajectoryRise);
	const float TanLaunchPitch = (4.0f * ApexAtFullCharge) / FMath::Max(1.0f, FarRange);

	// Flight time grows with the square root of range, as it does for a real arc, so the
	// preview and the shell stay in step and short shots arrive quickly instead of crawling.
	const float RangeFraction = FMath::Sqrt(FMath::Clamp(OutRange / FMath::Max(1.0f, FarRange), 0.0f, 1.0f));
	const float FlightSeconds = FMath::Max(0.05f, TrajectoryFlightSeconds * RangeFraction);

	const float HorizontalSpeed = OutRange / FlightSeconds;
	const float VerticalSpeed = HorizontalSpeed * TanLaunchPitch;
	// Solved from the arc rather than assumed, so the shell lands exactly at OutRange.
	OutGravityZ = FlightSeconds > 0.0f ? (-2.0f * VerticalSpeed) / FlightSeconds : 0.0f;
	OutInitialVelocity = PlanarDirection * HorizontalSpeed + FVector::UpVector * VerticalSpeed;
	return true;
}

FTransform ANavalHeavyWeaponActor::GetOperatorTransform() const
{
	return OperatorPoint ? OperatorPoint->GetComponentTransform() : GetActorTransform();
}

bool ANavalHeavyWeaponActor::TryFire(AActor* Requester, const FVector& AimLocation, float ChargeAlpha)
{
	if (!HasAuthority())
	{
		return false;
	}

	const float SanitizedCharge = FMath::Clamp(ChargeAlpha, 0.0f, 1.0f);
	FVector PreviewVelocity = FVector::ZeroVector;
	float PreviewGravityZ = 0.0f;
	float SanitizedRange = 0.0f;
	BuildChargedTrajectory(AimLocation, SanitizedCharge, PreviewVelocity, PreviewGravityZ, SanitizedRange);
	const FVector SanitizedAimLocation = GetMuzzleLocation()
		+ GetMuzzleDirection().GetSafeNormal2D() * SanitizedRange;

	FGameplayTag FailReason;
	if (!CanFire(Requester, SanitizedAimLocation, FailReason))
	{
		UE_LOG(
			LogNavalCore,
			Verbose,
			TEXT("[HeavyWeapon] Fire refused weapon=%s requester=%s reason=%s"),
			*GetName(),
			*GetNameSafe(Requester),
			*FailReason.ToString());
		return false;
	}

	PendingAimLocation = SanitizedAimLocation;
	PendingChargeAlpha = SanitizedCharge;
	NextFireServerTime = NavalTime::GetNetworkTimeSeconds(this) + FireWindupSeconds + ReloadSeconds;
	ForceNetUpdate();
	BroadcastWeaponState();

	// A paired window opens under the same input; the shell just waits out the shutter.
	const UNavalFireWindowComponent* PairedWindow = FindPairedWindow();
	const float WindupSeconds = FireWindupSeconds
		+ (PairedWindow ? PairedWindow->GetOpenWindupSeconds() : 0.0f);

	UE_LOG(
		LogNavalCore,
		Display,
		TEXT("[HeavyWeapon] Fire accepted weapon=%s requester=%s charge=%.2f range=%.0f windup=%.2fs paired_window=%d"),
		*GetName(),
		*GetNameSafe(Requester),
		SanitizedCharge,
		SanitizedRange,
		WindupSeconds,
		PairedWindow != nullptr);

	if (WindupSeconds <= 0.0f)
	{
		SpawnProjectile();
		return true;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			FireWindupTimerHandle,
			this,
			&ANavalHeavyWeaponActor::HandleFireWindupElapsed,
			WindupSeconds,
			false);
	}
	return true;
}

void ANavalHeavyWeaponActor::HandleFireWindupElapsed()
{
	SpawnProjectile();
}

void ANavalHeavyWeaponActor::SpawnProjectile()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || !ProjectileClass)
	{
		UE_LOG(
			LogNavalCore,
			Warning,
			TEXT("[HeavyWeapon] SpawnProjectile aborted weapon=%s world=%d authority=%d projectile_class=%s"),
			*GetName(), World != nullptr, HasAuthority(), *GetNameSafe(ProjectileClass));
		return;
	}

	const FVector MuzzleLocation = GetMuzzleLocation();
	FVector InitialVelocity = FVector::ZeroVector;
	float GravityZ = 0.0f;
	float ChargedRange = MaxRange;
	BuildChargedTrajectory(PendingAimLocation, PendingChargeAlpha, InitialVelocity, GravityZ, ChargedRange);
	const FVector Direction = InitialVelocity.GetSafeNormal(UE_SMALL_NUMBER, GetMuzzleDirection());

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Instigator = Cast<APawn>(WeaponOperator);
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ANavalProjectile* Projectile = World->SpawnActor<ANavalProjectile>(
		ProjectileClass, MuzzleLocation, Direction.Rotation(), SpawnParameters);
	if (!Projectile)
	{
		UE_LOG(
			LogNavalCore,
			Warning,
			TEXT("[HeavyWeapon] Projectile failed to spawn weapon=%s class=%s at=%s"),
			*GetName(), *GetNameSafe(ProjectileClass), *MuzzleLocation.ToCompactString());
		return;
	}

	FNavalProjectileLaunchParams Params;
	Params.InitialVelocity = InitialVelocity;
	Params.GravityZ = GravityZ;
	Params.SourceWeapon = this;
	Params.SourceOperator = WeaponOperator;
	Params.TeamId = GetTeamId();
	Params.MinimumRange = MinimumRange;
	Params.MaxRange = ChargedRange;
	Projectile->LaunchProjectile(Params);

	UE_LOG(
		LogNavalCore,
		Display,
		TEXT("[HeavyWeapon] Projectile launched weapon=%s projectile=%s muzzle=%s velocity=%s gravity=%.1f range=%.0f operator=%s"),
		*GetName(),
		*GetNameSafe(Projectile),
		*MuzzleLocation.ToCompactString(),
		*InitialVelocity.ToCompactString(),
		GravityZ,
		ChargedRange,
		*GetNameSafe(WeaponOperator.Get()));
}

void ANavalHeavyWeaponActor::HandlePartStateChanged(UNavalPartComponent* /*Part*/)
{
	if (PartComponent && !PartComponent->IsFunctional() && WeaponOperator != nullptr)
	{
		// A gun shot out from under its operator stops being a gun immediately.
		UE_LOG(
			LogNavalCore,
			Warning,
			TEXT("[HeavyWeapon] Part went non-functional, kicking operator weapon=%s operator=%s"),
			*GetName(),
			*GetNameSafe(WeaponOperator.Get()));
		ReleaseOperator(WeaponOperator);
	}
	BroadcastWeaponState();
}

void ANavalHeavyWeaponActor::BroadcastWeaponState() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FNavalHeavyWeaponMessage Message;
	Message.Weapon = const_cast<ANavalHeavyWeaponActor*>(this);
	Message.Operator = WeaponOperator;
	Message.WeaponState = PartComponent ? PartComponent->GetPartState() : ENavalPartState::Operational;
	Message.DurabilityFraction = PartComponent ? PartComponent->GetDurabilityFraction() : 1.0f;
	Message.ReloadSecondsRemaining = GetReloadSecondsRemaining();
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(
		NavalGameplayTags::Message_HeavyWeapon_State, Message);
}

bool ANavalHeavyWeaponActor::IsGroundSuitable(
	const UObject* WorldContextObject,
	const FVector& Location,
	float FootprintRadius,
	float MaxSlopeDegrees,
	FGameplayTag& OutFailReason)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		OutFailReason = NavalGameplayTags::Fail_GroundUnsuitable;
		return false;
	}

	// Four legs plus the centre. "No foundation required" still means the whole footprint has
	// to rest on ground that exists and is flat enough -- not a cliff edge, not deep water.
	const TArray<FVector2D> Offsets = {
		FVector2D(0.0, 0.0),
		FVector2D(FootprintRadius, 0.0),
		FVector2D(-FootprintRadius, 0.0),
		FVector2D(0.0, FootprintRadius),
		FVector2D(0.0, -FootprintRadius)
	};

	const double CosMaxSlope = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(MaxSlopeDegrees, 1.0f, 45.0f)));
	FCollisionQueryParams TraceParams(FName(TEXT("NavalGroundCheck")), /*bTraceComplex=*/false);

	double MinHeight = TNumericLimits<double>::Max();
	double MaxHeight = TNumericLimits<double>::Lowest();
	for (const FVector2D& Offset : Offsets)
	{
		const FVector Start = Location + FVector(Offset.X, Offset.Y, 250.0);
		const FVector End = Location + FVector(Offset.X, Offset.Y, -400.0);

		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, TraceParams))
		{
			OutFailReason = NavalGameplayTags::Fail_GroundUnsuitable;
			return false;
		}
		if (FVector::DotProduct(Hit.ImpactNormal, FVector::UpVector) < CosMaxSlope)
		{
			OutFailReason = NavalGameplayTags::Fail_GroundUnsuitable;
			return false;
		}

		MinHeight = FMath::Min(MinHeight, static_cast<double>(Hit.ImpactPoint.Z));
		MaxHeight = FMath::Max(MaxHeight, static_cast<double>(Hit.ImpactPoint.Z));
	}

	// A step or a crevice between the legs is as unusable as a slope, even when every
	// individual sample was flat.
	if (MaxHeight - MinHeight > FootprintRadius)
	{
		OutFailReason = NavalGameplayTags::Fail_GroundUnsuitable;
		return false;
	}

	return true;
}
